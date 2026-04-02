import type { SessionArchivePayload } from '../types'

const SESSION_AUTOSAVE_DB_NAME = 'bsl-host-session-autosave'
const SESSION_AUTOSAVE_DB_VERSION = 1
const SESSION_AUTOSAVE_STORE = 'handles'
const SESSION_AUTOSAVE_KEY = 'session-archive'
const SESSION_AUTOSAVE_FILE_NAME = 'bsl-session-live.json'

type SessionSaveFilePickerOptions = {
  suggestedName?: string
  types?: Array<{
    description?: string
    accept: Record<string, string[]>
  }>
}

type SessionHandlePermissionDescriptor = {
  mode?: 'read' | 'readwrite'
}

type WindowWithFilePicker = Window & {
  showSaveFilePicker?: (
    options?: SessionSaveFilePickerOptions,
  ) => Promise<FileSystemFileHandle>
}

type PermissionCapableHandle = FileSystemFileHandle & {
  queryPermission?: (
    descriptor?: SessionHandlePermissionDescriptor,
  ) => Promise<PermissionState>
  requestPermission?: (
    descriptor?: SessionHandlePermissionDescriptor,
  ) => Promise<PermissionState>
}

function openAutosaveDatabase(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const request = window.indexedDB.open(
      SESSION_AUTOSAVE_DB_NAME,
      SESSION_AUTOSAVE_DB_VERSION,
    )

    request.onerror = () => {
      reject(request.error ?? new Error('Unable to open the autosave database.'))
    }

    request.onupgradeneeded = () => {
      const database = request.result

      if (!database.objectStoreNames.contains(SESSION_AUTOSAVE_STORE)) {
        database.createObjectStore(SESSION_AUTOSAVE_STORE)
      }
    }

    request.onsuccess = () => resolve(request.result)
  })
}

async function withAutosaveStore<T>(
  mode: IDBTransactionMode,
  action: (store: IDBObjectStore) => IDBRequest<T>,
): Promise<T> {
  const database = await openAutosaveDatabase()

  return new Promise<T>((resolve, reject) => {
    const transaction = database.transaction(SESSION_AUTOSAVE_STORE, mode)
    const store = transaction.objectStore(SESSION_AUTOSAVE_STORE)
    const request = action(store)

    request.onerror = () => {
      reject(request.error ?? new Error('IndexedDB request failed.'))
    }

    transaction.onerror = () => {
      reject(transaction.error ?? new Error('IndexedDB transaction failed.'))
    }

    transaction.oncomplete = () => {
      database.close()
      resolve(request.result)
    }

    transaction.onabort = () => {
      database.close()
    }
  })
}

export function supportsSessionAutosave(): boolean {
  return (
    typeof window !== 'undefined' &&
    'indexedDB' in window &&
    typeof (window as WindowWithFilePicker).showSaveFilePicker === 'function'
  )
}

export async function loadSessionAutosaveHandle(): Promise<FileSystemFileHandle | null> {
  if (!supportsSessionAutosave()) {
    return null
  }

  const result = await withAutosaveStore<FileSystemFileHandle | undefined>(
    'readonly',
    (store) => store.get(SESSION_AUTOSAVE_KEY),
  )

  return result ?? null
}

export async function saveSessionAutosaveHandle(
  handle: FileSystemFileHandle,
): Promise<void> {
  if (!supportsSessionAutosave()) {
    return
  }

  await withAutosaveStore<IDBValidKey>(
    'readwrite',
    (store) => store.put(handle, SESSION_AUTOSAVE_KEY),
  )
}

export async function clearSessionAutosaveHandle(): Promise<void> {
  if (!supportsSessionAutosave()) {
    return
  }

  await withAutosaveStore<undefined>(
    'readwrite',
    (store) => store.delete(SESSION_AUTOSAVE_KEY),
  )
}

export async function pickSessionAutosaveHandle(): Promise<FileSystemFileHandle> {
  const picker = (window as WindowWithFilePicker).showSaveFilePicker

  if (picker === undefined) {
    throw new Error('This browser does not support file-backed session autosave.')
  }

  return picker({
    suggestedName: SESSION_AUTOSAVE_FILE_NAME,
    types: [
      {
        description: 'BSL session archive',
        accept: {
          'application/json': ['.json'],
        },
      },
    ],
  })
}

export async function ensureSessionAutosavePermission(
  handle: FileSystemFileHandle,
  interactive = false,
): Promise<boolean> {
  const permissionHandle = handle as PermissionCapableHandle
  const descriptor = { mode: 'readwrite' as const }

  if (permissionHandle.queryPermission === undefined) {
    return true
  }

  const currentPermission = await permissionHandle.queryPermission(descriptor)
  if (currentPermission === 'granted') {
    return true
  }

  if (!interactive || permissionHandle.requestPermission === undefined) {
    return false
  }

  return (await permissionHandle.requestPermission(descriptor)) === 'granted'
}

export async function writeSessionAutosaveFile(
  handle: FileSystemFileHandle,
  payload: SessionArchivePayload,
): Promise<void> {
  const writable = await handle.createWritable()

  try {
    await writable.write(JSON.stringify(payload, null, 2))
  } finally {
    await writable.close()
  }
}

export function describeSessionAutosaveError(error: unknown): string {
  if (error instanceof DOMException) {
    if (error.name === 'AbortError') {
      return 'Autosave file selection was cancelled.'
    }

    if (error.name === 'NotAllowedError') {
      return 'Browser write permission for the autosave file was not granted.'
    }

    if (error.name === 'NotFoundError') {
      return 'The previously selected autosave file is no longer available.'
    }
  }

  return error instanceof Error
    ? error.message
    : 'Session autosave failed for an unknown reason.'
}
