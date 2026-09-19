// SOH [Unbound] [WASM] The host-facing side of the Unbound converter (appended to soh-unbound-convert.js as
// --post-js, so `Module` here is the instance the factory returns). The contract is in soh/wasm/HOST-API.md §7;
// keep this file free of any assumption about who is calling.
//
//   const mod = await createSohUnboundConverter();
//   const { bytes, report } = await mod.convertToUnbound(ootBytes);

(function () {
    var SOURCE_PATH = '/work/oot.o2r';
    var OUT_PATH = '/work/oot-unbound.o2r';

    function unlinkIfPresent(path) {
        try {
            Module['FS'].unlink(path);
        } catch (e) {}
    }

    function readResult() {
        return JSON.parse(Module['ccall']('Unbound_ConvertResultJson', 'string'));
    }

    // Runs one conversion. Resolves to { bytes, report }; rejects with an Error whose message is the reason
    // and whose `code` is the converter's status code. One conversion per instance.
    Module['convertToUnbound'] = function (ootBytes) {
        return new Promise(function (resolve, reject) {
            var FS = Module['FS'];
            var code, report, bytes;
            // One funnel for everything that can throw: a trap, an uncaught exception, a result that will
            // not parse. Whatever happens, neither archive stays in the VFS.
            try {
                if (!FS.analyzePath('/work').exists) FS.mkdir('/work');
                FS.writeFile(SOURCE_PATH, ootBytes);
                code = Module['ccall']('Unbound_ConvertArchive', 'number', ['string', 'string'], [SOURCE_PATH, OUT_PATH]);
                report = readResult();
                if (code === 0) bytes = FS.readFile(OUT_PATH);
            } catch (e) {
                var error = new Error('Conversion failed: ' + ((e && e.message) || String(e)));
                error.code = -5;
                reject(error);
                return;
            } finally {
                unlinkIfPresent(SOURCE_PATH);
                unlinkIfPresent(OUT_PATH);
            }
            if (code !== 0) {
                var failure = new Error(report.error || 'Conversion failed.');
                failure.code = code;
                failure.report = report;
                reject(failure);
                return;
            }
            resolve({ bytes: bytes, report: report });
        });
    };
})();
