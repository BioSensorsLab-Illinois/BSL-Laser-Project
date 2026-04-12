declare module '@tauri-apps/api/core' {
  export function invoke<T = unknown>(cmd: string, args?: Record<string, unknown>): Promise<T>
}

declare module '@tauri-apps/api/event' {
  export type UnlistenFn = () => void

  export function listen<T = unknown>(
    event: string,
    handler: (event: { payload: T }) => void,
  ): Promise<UnlistenFn>
}
