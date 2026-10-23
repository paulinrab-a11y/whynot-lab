// O resumo do projeto que a IA lê primeiro (get_project { view: "resumo" }): curto, em texto, feito do JSON do .wnl
// que a ponte devolve. Fica no Node de propósito: mudar o resumo não pede build do app.

const NOTE_NAMES = ['Dó', 'Dó#', 'Ré', 'Ré#', 'Mi', 'Fá', 'Fá#', 'Sol', 'Sol#', 'Lá', 'Lá#', 'Si'];
const LETTER_NAMES: Record<string, string> = { C: 'Dó', D: 'Ré', E: 'Mi', F: 'Fá', G: 'Sol', A: 'Lá', B: 'Si' };
const SCALE_NAMES: Record<string, string> = { minor: 'menor', major: 'maior', harmonicMinor: 'menor harmônica', dorian: 'dórica', phrygian: 'frígia' };

export const noteName = (key: number) => `${NOTE_NAMES[((key % 12) + 12) % 12]}${Math.floor(key / 12) - 1}`;

const keyName = (root: string, scale: string) => {
  const letter = LETTER_NAMES[root?.[0]?.toUpperCase() ?? ''] ?? root ?? '?';
  return `${letter}${root?.slice(1) ?? ''} ${SCALE_NAMES[scale] ?? scale ?? ''}`.trim();
};

const baseName = (path: string | undefined) => (path ?? '').split(/[\\/]/).pop() ?? '';
const db = (v: number | undefined) => `${(v ?? 0).toFixed(1).replace('.', ',')} dB`;

function limited<T>(items: T[], max: number, line: (item: T) => string): string[] {
  const out = items.slice(0, max).map(line);

  if (items.length > max)
    out.push(`  ... e mais ${items.length - max}`);

  return out;
}

export function summarizeProject(project: any, status?: any): string {
  const p = project?.project ?? {};
  const [num, den] = Array.isArray(p.timeSignature) ? p.timeSignature : [4, 4];
  const ticksPerBar = (num * 960 * 4) / (den || 4);
  const bar = (t: number) => Math.floor(t / ticksPerBar) + 1;

  const channels: any[] = project?.channels ?? [];
  const patterns: any[] = project?.patterns ?? [];
  const tracks: any[] = project?.mixer?.tracks ?? [];
  const items: any[] = project?.playlist?.items ?? [];
  const lanesOnPlaylist: any[] = project?.playlist?.tracks ?? [];

  const channelName = (id: string) => channels.find((c) => c.id === id)?.name ?? id;
  const trackLabel = (id: string) => {
    if (!id || id === 'mx_master') return 'Master';
    if (id === 'device') return 'placa de som';
    const t = tracks.find((x) => x.id === id);
    return t ? `${t.id} "${t.name}"` : id;
  };

  const lines: string[] = [];

  // cabeçalho e estado
  const head = [`Projeto "${p.name ?? '?'}"`, `${String(p.bpm ?? '?').replace('.', ',')} BPM`, `${num}/${den}`];

  if (p.key)
    head.push(keyName(p.key.root, p.key.scale));

  const current = patterns.find((x) => x.id === p.currentPattern);
  head.push(p.songMode ? 'SONG (toca a playlist)' : `PAT (padrão atual ${p.currentPattern ?? '?'}${current ? ` "${current.name}"` : ''})`);
  lines.push(head.join(' · '));

  if (status) {
    const state = [status.playing ? 'tocando' : 'parado',
                   status.dirty ? 'alterado, não salvo' : 'sem alterações desde o último salvar',
                   status.file ? `arquivo ${status.file}` : 'nunca salvo'];

    if (status.headless)
      state.push('sem janela: não há autosave, salve com project.save ou project.saveNewVersion');

    lines.push(state.join(' · '));

    if (status.undo || status.redo)
      lines.push(`Desfazer: "${status.undo || '—'}" · Refazer: "${status.redo || '—'}"`);
  }

  // canais
  lines.push('', `Canais (${channels.length}):`);
  lines.push(...limited(channels, 40, (c) => {
    let what: string;

    switch (c.kind) {
      case 'synth': what = `WHYNOT Synth (preset ${c.synth?.preset || 'próprio'})`; break;
      case 'plugin': what = `VST3 "${c.plugin?.name ?? '?'}"`; break;
      case 'audio': what = `canal de áudio (${baseName(c.sample?.path)})`; break;
      default: {
        const s = c.sampler ?? {};
        what = `sampler (${baseName(c.sample?.path) || 'sem sample'}${s.mono ? `, mono, glide ${s.glideMs ?? 80} ms` : ''})`;
      }
    }

    return `- ${c.id} "${c.name}" · ${what} → ${trackLabel(c.output)}${c.mute ? ' · mudo' : ''}`;
  }));

  // padrões
  lines.push('', `Padrões (${patterns.length}):`);
  lines.push(...limited(patterns, 20, (pat) => {
    const lanes = Object.entries(pat.lanes ?? {}).map(([id, lane]: [string, any]) => {
      const parts: string[] = [];

      if (lane.steps?.length)
        parts.push(`${lane.steps.length} passos`);

      if (lane.notes?.length) {
        const keys = lane.notes.map((n: any) => n.key);
        const lo = Math.min(...keys), hi = Math.max(...keys);
        parts.push(`${lane.notes.length} notas (${lo === hi ? noteName(lo) : `${noteName(lo)} a ${noteName(hi)}`})`);
      }

      return parts.length ? `${channelName(id)} ${parts.join(' e ')}` : null;
    }).filter(Boolean);

    const mark = pat.id === p.currentPattern ? ' (atual)' : '';
    return `- ${pat.id} "${pat.name}"${mark} · ${pat.lengthSteps ?? 16} passos: ${lanes.length ? lanes.join('; ') : 'vazio'}`;
  }));

  // playlist
  const lastBar = items.reduce((m, it) => Math.max(m, bar((it.start ?? 0) + (it.length ?? 0) - 1)), 0);
  lines.push('', `Playlist: ${items.length} blocos em ${lanesOnPlaylist.length} raias${items.length ? `, até o compasso ${lastBar}` : ''}${p.songMode ? '' : ' (SONG desligado)'}`);
  lines.push(...limited([...items].sort((a, b) => (a.start ?? 0) - (b.start ?? 0)), 15, (it) => {
    const name = it.kind === 'audio' ? `áudio de "${channelName(it.ref)}"` : `"${patterns.find((x) => x.id === it.ref)?.name ?? it.ref}"`;
    const lane = lanesOnPlaylist.find((l) => l.id === it.track)?.name ?? it.track;
    return `- ${it.id} ${name} na raia "${lane}", compassos ${bar(it.start ?? 0)} a ${bar((it.start ?? 0) + (it.length ?? 1) - 1)}`;
  }));

  // mixer
  lines.push('', `Mixer (${tracks.length} trilhas):`);
  lines.push(...limited(tracks, 40, (t) => {
    const fx = (t.inserts ?? []).map((i: any) => `${i.kind === 'vst3' ? `VST3 "${i.plugin?.name ?? i.type}"` : i.type}${i.bypass ? ' (bypass)' : ''}${i.sidechain ? ` (sidechain ${trackLabel(i.sidechain)})` : ''}`);
    const sends = (t.sends ?? []).map((s: any) => `${trackLabel(s.to)} ${db(s.db)}`);
    const flags = [t.mute ? 'mudo' : '', t.solo ? 'solo' : '', t.frozen ? 'congelada' : '', t.role ? `papel ${t.role}` : ''].filter(Boolean);

    return `- ${t.id} "${t.name}" ${db(t.volumeDb)}${t.pan ? `, pan ${t.pan}` : ''}${flags.length ? ` (${flags.join(', ')})` : ''}`
         + ` · ${fx.length ? fx.join(', ') : 'sem efeitos'}${sends.length ? ` · envios: ${sends.join(', ')}` : ''}`
         + (t.id === 'mx_master' ? '' : ` → ${trackLabel(t.output)}`);
  }));

  const automation: any[] = Array.isArray(project?.automation) ? project.automation : [];

  if (automation.length) {
    lines.push('', `Automação (${automation.length} curvas):`);
    lines.push(...limited(automation, 20, (a) => {
      const t = a.target ?? {};
      const where = t.kind === 'channel' ? `canal "${channelName(t.channel)}"` : trackLabel(t.track);
      return `- ${a.id}: ${t.param ?? '?'}${t.insert ? ` do efeito ${t.insert}` : ''} em ${where} (${a.points?.length ?? 0} pontos)`;
    }));
  }

  return lines.join('\n');
}
