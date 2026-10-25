// Keep the user-selected installer in the browser for later visits to this origin.
const installerStore = (() => {
  const databaseName = 'openlf2-installer';
  const storeName = 'files';
  const key = 'LF2_v2.0a.exe';

  function open() {
    return new Promise((resolve, reject) => {
      const request = indexedDB.open(databaseName, 1);
      request.onupgradeneeded = () => request.result.createObjectStore(storeName);
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error);
    });
  }

  async function transact(mode, operation) {
    const database = await open();
    try {
      return await new Promise((resolve, reject) => {
        const transaction = database.transaction(storeName, mode);
        const request = operation(transaction.objectStore(storeName));
        transaction.oncomplete = () => resolve(request.result);
        request.onerror = () => reject(request.error);
        transaction.onerror = () => reject(transaction.error);
        transaction.onabort = () => reject(transaction.error);
      });
    } finally {
      database.close();
    }
  }

  return {
    load: () => transact('readonly', store => store.get(key)),
    save: file => transact('readwrite', store => store.put(file, key)),
    remove: () => transact('readwrite', store => store.delete(key))
  };
})();

// Downloads the LF2 v2.0a installer from archive.org (the only source that allows cross-origin requests), reporting
// progress and checking size and SHA-256.
const installerDownload = async onProgress => {
  const size = 29586263;
  const sha256 = '540c485547e234b0d09595f54d067aafd7176092c308aca61868f3cbc03030e1';
  const response = await fetch('https://archive.org/download/little-fighter-2_202404/Little%20Fighter%202.rar/Little%20Fighter%202%2FLF2Setup.exe');
  if (!response.ok) throw new Error('HTTP ' + response.status);
  const chunks = [];
  let received = 0;
  const reader = response.body.getReader();
  for (;;) {
    const {done, value} = await reader.read();
    if (done) break;
    chunks.push(value);
    received += value.length;
    onProgress(received, size);
  }
  const blob = new Blob(chunks);
  if (blob.size !== size) throw new Error('received ' + blob.size + ' bytes, expected ' + size);
  if (crypto.subtle) {
    const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', await blob.arrayBuffer()));
    if (Array.from(digest, byte => byte.toString(16).padStart(2, '0')).join('') !== sha256) throw new Error('checksum mismatch');
  }
  return blob;
};
