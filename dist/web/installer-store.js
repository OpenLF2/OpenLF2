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
