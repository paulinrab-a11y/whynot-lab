// A ponte do servidor MCP com o app (Fase 4): o WebSocket local da WHYNOT, com o token do arquivo de controle.
// Liga só na primeira tool. No modo --attach, usa o app que você abriu com --control <porta>; no modo
// --headless, abre o próprio app sem janela, com --parent-pid apontando para este processo (se este processo
// morrer, o app fecha sozinho). Ver docs/fase-4-plano.md, seção 4.1.

import { spawn, type ChildProcess } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

export const PROTOCOL = 1;

export interface CommandInfo {
  id: string;
  title: string;
  params: JsonSchemaObject;
  undoable: boolean;
  domain: string;
  risk: 'free' | 'confirm' | 'blocked';
  why?: string;
  paths?: string[];
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

    return new Promise((resolve, reject) => {
      const restart = (ms: number) => setTimeout(() => {
        this.pending.delete(id);
        reject(new BridgeError(`A WHYNOT não respondeu em ${Math.round(ms / 1000)} s. O comando pode ter rodado: confira com get_project.`));
      }, ms);

      this.pending.set(id, { resolve, reject, timer: restart(timeoutMs), restart });
      ws.send(JSON.stringify({ jsonrpc: '2.0', id, method, params }));
    }).then((reply: any) => {
      if (reply.error)
        throw new BridgeError(`${reply.error.message} (erro ${reply.error.code} da ponte)`);

      return reply.result;
    });
  }

  async commands(): Promise<CommandInfo[]> {
    if (this.cachedCommands === null)
      this.cachedCommands = (await this.call('command.list')).commands as CommandInfo[];

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

    const { port, token } = JSON.parse(readFileSync(controlFile, 'utf8'));
    const ws = new WebSocket(`ws://127.0.0.1:${port}/`, { headers: { Authorization: `Bearer ${token}` } } as any);

    await new Promise<void>((resolve, reject) => {
      ws.onopen = () => resolve();
      ws.onerror = () => reject(new BridgeError('Não consegui ligar na ponte da WHYNOT: o app fechou, a pessoa desligou a IA no pé da janela '
                                              + '("Ligar a IA" volta), ou o arquivo de controle é de uma sessão antiga.'));
    });

    ws.onmessage = (event) => {
      let reply: any;

      try {
        reply = JSON.parse(typeof event.data === 'string' ? event.data : Buffer.from(event.data as ArrayBuffer).toString('utf8'));
      } catch {
        return;
      }

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
    const hello = await this.call('hello', { client: 'whynot-mcp', version: '0.1.0', protocol: PROTOCOL });
    this.log(`ligado na ${hello.app} ${hello.version} (protocolo ${hello.protocol}, ${hello.headless ? 'sem janela' : 'com janela'}, `
           + `confirmação ${hello.confirmations === 'window' ? 'na janela' : 'pelo cliente de IA'})`);
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
