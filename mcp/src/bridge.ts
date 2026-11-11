// A ponte do servidor MCP com o app (Fase 4): o WebSocket local da WHYNOT, com o token do arquivo de controle.
// Liga só na primeira tool. No modo --attach, usa o app que você abriu com --control <porta>; no modo
// --headless, abre o próprio app sem janela, com --parent-pid apontando para este processo (se este processo
// morrer, o app fecha sozinho). Ver docs/fase-4-plano.md, seção 4.1.

import { spawn, type ChildProcess } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

export const PROTOCOL = 1;

/** O teto de um pedido à ponte (limits::messageInBytes do app, 1 MB): acima disso o app fecha a conexão com 1009. */
export const MAX_REQUEST_BYTES = 1024 * 1024;

const CONNECT_TIMEOUT_MS = 10_000;

export interface CommandInfo {
  id: string;
  title: string;
  params: JsonSchemaObject;
  undoable: boolean;
  domain: string;
  risk: 'free' | 'confirm' | 'blocked';
  why?: string;
  paths?: string[];
  confirmsWhen?: string;   // livre, mas pede a confirmação da pessoa nestes casos
}

export interface JsonSchemaObject {
  type?: string;
  properties?: Record<string, { type?: string; description?: string }>;
  required?: string[];
}

export interface BridgeOptions {
  mode: 'attach' | 'headless';
  port?: number;          // attach: a porta do --control
  exe?: string;           // headless: o WHYNOT LAB.exe
  dataFolder?: string;    // headless: --data (vazio = a pasta de dados de verdade)
  controlFile?: string;   // headless: --control-file (vazio = uma pasta temporária)
  log?: (line: string) => void;
}

/** O que a ponte diz quando não deu: vai para a IA como isError, em português. */
export class BridgeError extends Error {}

/** O que as tools usam (o teste sem app troca por uma de mentira). */
export interface BridgeLike {
  call(method: string, params?: object, timeoutMs?: number): Promise<any>;
  commands(): Promise<CommandInfo[]>;
}

interface Pending {
  resolve: (value: any) => void;
  reject: (error: Error) => void;
  timer: NodeJS.Timeout;
  restart: (ms: number) => NodeJS.Timeout;   // o request.waiting do app estica a espera
}

const sleep = (ms: number) => new Promise((done) => setTimeout(done, ms));

/** O caminho do projeto aberto, pelo que passou na ponte: o hello, o app.status (get_project) e os comandos
    project.new, project.open, project.save e project.saveNewVersion que deram certo. '' = nunca salvo; null = não sei. */
export function projectFileAfter(previous: string | null, method: string, params: any, result: any): string | null {
  if (method === 'app.status')
    return typeof result?.file === 'string' ? result.file : previous;

  if (method === 'hello')
    return typeof result?.project?.file === 'string' ? result.project.file : previous;

  if (method !== 'command.run' || !result?.ok)
    return previous;

  if (params?.id === 'project.new')
    return '';

  if (params?.id === 'project.open' || params?.id === 'project.save' || params?.id === 'project.saveNewVersion') {
    // o saveNewVersion devolve o caminho no value; o open e o save, só nos parâmetros (save sem path = o arquivo atual)
    const path = typeof result.value?.path === 'string' && result.value.path ? result.value.path : params?.params?.path;
    return typeof path === 'string' && path ? path : previous;
  }

  return previous;
}

/** O aviso, uma vez só, de que o app sem janela caiu e foi aberto de novo com outro projeto (vazio).
    Vai como erro da tool: o pedido que chegou era para o projeto antigo e não roda. */
export function crashNotice(exitCode: string | null, lastFile: string | null): string {
  const code = exitCode && exitCode !== 'null' && exitCode !== '0' ? ` (saiu com o código ${exitCode})` : '';
  const reopen = lastFile
    ? `O último projeto que eu conhecia era ${lastFile}: reabra com command_run { id: "project.open", params: { path: ${JSON.stringify(lastFile)} } }.`
    : 'Não sei de arquivo salvo do projeto que estava aberto: se ele foi salvo, reabra com project.open <caminho>.';

  return `A WHYNOT LAB fechou inesperadamente${code}. Abri de novo, com um projeto vazio: o que não tinha sido salvo se perdeu, `
       + `e este pedido NÃO rodou. ${reopen} Confira com get_project antes de continuar, e avise a pessoa.`;
}

export class Bridge implements BridgeLike {
  private ws: WebSocket | null = null;
  private connecting: Promise<void> | null = null;
  private pending = new Map<number, Pending>();
  private nextId = 0;
  private cachedCommands: CommandInfo[] | null = null;
  private app: ChildProcess | null = null;
  private appExit: string | null = null;
  private tempDir: string | null = null;
  private closed = false;
  private lastProjectFile: string | null = null;
  private appWasLive = false;              // o app sem janela atual chegou a responder o hello
  private crashed: string | null = null;   // o aviso da queda, à espera da próxima chamada
  readonly options: BridgeOptions;

  constructor(options: BridgeOptions) {
    this.options = options;
  }

  private log(line: string) {
    this.options.log?.(line);
  }

  /** O pid do app sem janela que este servidor abriu (para medir a RAM no teste). */
  get appPid(): number | undefined {
    return this.app?.pid;
  }

  async call(method: string, params: object = {}, timeoutMs = 30000): Promise<any> {
    await this.ensureConnected();
    const ws = this.ws!;
    const id = ++this.nextId;
    const message = JSON.stringify({ jsonrpc: '2.0', id, method, params });
    const bytes = Buffer.byteLength(message);

    // maior que o teto, o app fecharia a conexão (1009) e o erro pareceria uma queda
    if (bytes > MAX_REQUEST_BYTES)
      throw new BridgeError(`Pedido grande demais para a ponte da WHYNOT: ${Math.ceil(bytes / 1024)} KB, e o teto é ${MAX_REQUEST_BYTES / 1024} KB. `
                          + 'Nada rodou: divida em pedidos menores.');

    return new Promise((resolve, reject) => {
      const restart = (ms: number) => setTimeout(() => {
        this.pending.delete(id);
        reject(new BridgeError(`A WHYNOT não respondeu em ${Math.round(ms / 1000)} s. O comando pode ter rodado: confira com get_project.`));
      }, ms);

      this.pending.set(id, { resolve, reject, timer: restart(timeoutMs), restart });
      ws.send(message);
    }).then((reply: any) => {
      if (reply.error)
        throw new BridgeError(`${reply.error?.message ?? 'Erro sem mensagem'} (erro ${reply.error?.code ?? '?'} da ponte)`);

      if (!('result' in reply))
        throw new BridgeError('A WHYNOT respondeu fora do formato (sem result nem error). O pedido pode ter rodado: confira com get_project.');

      this.lastProjectFile = projectFileAfter(this.lastProjectFile, method, params, reply.result);
      return reply.result;
    });
  }

  async commands(): Promise<CommandInfo[]> {
    if (this.cachedCommands === null) {
      const list = (await this.call('command.list'))?.commands;

      if (!Array.isArray(list))
        throw new BridgeError('A WHYNOT não devolveu a lista de comandos no formato esperado (o app e o servidor MCP são de versões diferentes?).');

      this.cachedCommands = list as CommandInfo[];
    }

    return this.cachedCommands;
  }

  /** Fecha a conexão; no modo sem janela, fecha também o app (app.quit, e à força se ele não sair). */
  async close(): Promise<void> {
    if (this.closed)
      return;

    // o app.quit sai antes de marcar como fechado: depois disso, a ponte recusa pedidos novos
    if (this.app && this.appExit === null && this.ws?.readyState === WebSocket.OPEN) {
      const exited = new Promise((done) => this.app!.once('exit', done));
      await Promise.race([this.call('app.quit', {}, 3000).catch(() => {}), sleep(3000)]);
      await Promise.race([exited, sleep(5000)]);
    }

    this.closed = true;

    if (this.app && this.appExit === null) {
      // não saiu sozinho: fecha à força, e o arquivo de controle (que o app apagaria) sai daqui
      this.app.kill();
      rmSync(this.options.controlFile ?? join(this.tempDir ?? '', 'control.json'), { force: true });
    }

    this.ws?.close();

    if (this.tempDir)
      rmSync(this.tempDir, { recursive: true, force: true });
  }

  //--------------------------------------------------------------------------
  private async ensureConnected(): Promise<void> {
    if (this.closed)
      throw new BridgeError('O servidor MCP da WHYNOT está fechando.');

    if (this.ws?.readyState === WebSocket.OPEN)
      return;

    if (!this.connecting)
      this.connecting = this.connect().finally(() => { this.connecting = null; });

    return this.connecting;
  }

  private async connect(): Promise<void> {
    const controlFile = this.options.mode === 'headless' ? await this.startApp() : this.attachedControlFile();

    if (!existsSync(controlFile))
      throw new BridgeError(`A WHYNOT não está aberta com --control ${this.options.port ?? 47830}. Abra o app pelo atalho com --control, ou use o servidor no modo --headless.`);

    let port: unknown = undefined;
    let token: unknown = undefined;

    try {
      const control = JSON.parse(readFileSync(controlFile, 'utf8'));
      port = control?.port;
      token = control?.token;
    } catch {
      // cai no erro de baixo
    }

    if (typeof port !== 'number' || typeof token !== 'string')
      throw new BridgeError(`O arquivo de controle da WHYNOT (${controlFile}) está ilegível ou incompleto. Feche e abra o app de novo.`);

    const ws = new WebSocket(`ws://127.0.0.1:${port}/`, { headers: { Authorization: `Bearer ${token}` } } as any);

    await new Promise<void>((resolve, reject) => {
      // a ponte aceitou a ligação e não respondeu o pedido de WebSocket: sem prazo, a tool ficaria presa
      const timer = setTimeout(() => {
        ws.close();
        reject(new BridgeError(`A ponte da WHYNOT não respondeu em ${CONNECT_TIMEOUT_MS / 1000} s ao ligar. Nada rodou: tente de novo, ou feche e abra o app.`));
      }, CONNECT_TIMEOUT_MS);

      ws.onopen = () => { clearTimeout(timer); resolve(); };
      ws.onerror = () => {
        clearTimeout(timer);
        // o WebSocket do Node não diz o código HTTP da recusa (401 token, 403 origem, 429 pausa, 503 IA desligada):
        // os motivos vão juntos na mensagem
        reject(new BridgeError('Não consegui ligar na ponte da WHYNOT: o app fechou, a pessoa desligou a IA no pé da janela '
                             + '("Ligar a IA" volta), ou o arquivo de controle é de uma sessão antiga.'));
      };
    });

    ws.onmessage = (event) => {
      let reply: any;

      try {
        reply = JSON.parse(typeof event.data === 'string' ? event.data : Buffer.from(event.data as ArrayBuffer).toString('utf8'));
      } catch {
        return;
      }

      if (!reply || typeof reply !== 'object')
        return;   // JSON que não é objeto (null, número): não é resposta de ninguém, e ler campo dele derrubaria o servidor

      // o app perguntou à pessoa na janela (seção 5.5): espera o prazo dela, com folga
      if (reply.method === 'request.waiting' && reply.params) {
        const asked = this.pending.get(reply.params.id);

        if (asked) {
          clearTimeout(asked.timer);
          asked.timer = asked.restart((Number(reply.params.seconds) || 120) * 1000 + 15_000);
          this.log(`esperando a confirmação da pessoa na janela (até ${reply.params.seconds} s)`);
        }

        return;
      }

      const waiting = this.pending.get(reply.id);

      if (waiting) {
        clearTimeout(waiting.timer);
        this.pending.delete(reply.id);
        waiting.resolve(reply);
      }
    };

    ws.onclose = () => {
      if (this.ws === ws)
        this.ws = null;

      this.cachedCommands = null;

      for (const [id, waiting] of this.pending) {
        clearTimeout(waiting.timer);
        waiting.reject(new BridgeError('A WHYNOT fechou a conexão no meio do pedido (o app fechou, ou outra IA ligou no lugar).'));
        this.pending.delete(id);
      }
    };

    this.ws = ws;
    let hello: any;

    try {
      hello = await this.call('hello', { client: 'whynot-mcp', version: '0.1.0', protocol: PROTOCOL });
    } catch (error) {
      ws.close();   // sem hello (protocolo diferente) a ligação não serve: a próxima chamada tenta do começo
      throw error;
    }

    this.log(`ligado na ${hello?.app} ${hello?.version} (protocolo ${hello?.protocol}, ${hello?.headless ? 'sem janela' : 'com janela'}, `
           + `confirmação ${hello?.confirmations === 'window' ? 'na janela' : 'pelo cliente de IA'})`);

    if (this.options.mode === 'headless')
      this.appWasLive = true;

    // o app anterior caiu e este é outro, com projeto vazio: a chamada que chegou não roda, e a IA ouve o porquê,
    // uma vez. A ligação fica de pé: a chamada seguinte já segue no app novo.
    if (this.crashed !== null) {
      const notice = this.crashed;
      this.crashed = null;
      throw new BridgeError(notice);
    }
  }

  private attachedControlFile(): string {
    const appData = process.env.APPDATA ?? join(process.env.USERPROFILE ?? '', 'AppData', 'Roaming');
    return join(appData, 'WHYNOT LAB', 'control', `${this.options.port ?? 47830}.json`);
  }

  private async startApp(): Promise<string> {
    if (this.app && this.appExit === null)
      return this.options.controlFile ?? join(this.tempDir!, 'control.json');

    const exe = this.options.exe;

    if (!exe || !existsSync(exe))
      throw new BridgeError(`Não achei o WHYNOT LAB.exe para abrir sem janela (${exe ?? 'sem --exe'}).`);

    if (!this.options.controlFile && !this.tempDir)
      this.tempDir = mkdtempSync(join(tmpdir(), 'whynot-mcp-'));

    // o app anterior chegou a ligar e saiu sem ninguém pedir (o close() não passa por aqui): caiu.
    // O aviso guarda o caminho do projeto de antes; ninguém reabre o projeto sozinho.
    if (this.appWasLive) {
      this.crashed = crashNotice(this.appExit, this.lastProjectFile);
      this.appWasLive = false;
      this.lastProjectFile = null;
      this.log(`o app sem janela caiu (saiu com ${this.appExit}); abrindo outro, com projeto vazio`);
    }

    const controlFile = this.options.controlFile ?? join(this.tempDir!, 'control.json');
    rmSync(controlFile, { force: true });

    const args = ['--headless', '--control', '0', '--control-file', controlFile, '--parent-pid', String(process.pid)];

    if (this.options.dataFolder)
      args.push('--data', this.options.dataFolder);

    this.appExit = null;
    this.app = spawn(exe, args, { stdio: 'ignore', windowsHide: true });
    this.app.once('exit', (code) => {
      this.appExit = String(code);
      this.log(`o app sem janela saiu com ${code}`);
    });

    const started = Date.now();

    while (!existsSync(controlFile)) {
      if (this.appExit !== null) {
        const why = controlFile + '.erro.txt';
        throw new BridgeError(`A WHYNOT sem janela saiu com ${this.appExit}${existsSync(why) ? ': ' + readFileSync(why, 'utf8') : ''}.`);
      }

      if (Date.now() - started > 60000)
        throw new BridgeError('A WHYNOT sem janela não abriu a ponte em 60 s.');

      await sleep(100);
    }

    await sleep(50);   // o arquivo é gravado de forma atômica; a espera é só folga
    this.log(`app sem janela aberto em ${Date.now() - started} ms (pid ${this.app.pid})`);
    return controlFile;
  }
}
