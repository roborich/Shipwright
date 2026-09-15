// SOH [WASM] The host-facing side of the converter (appended to soh-extract.js as --post-js,
// so `Module` here is the instance the factory returns). The contract is in
// soh/wasm/HOST-API.md; keep this file free of any assumption about who is calling.
//
//   const mod = await createSohExtractor();
//   const { name, version, bytes } = await mod.extractRom(romBytes, { onProgress });

(function () {
    var ROM_PATH = '/rom/rom.z64';
    var OUT_DIR = '/out';
    var PROGRESS_LINE = /^\((\d+) \/ (\d+)\): /;
    var used = false;

    function mkdirIfMissing(path) {
        try {
            Module['FS'].mkdir(path);
        } catch (e) {
            if (!e || e.code !== 'EEXIST') throw e;
        }
    }

    // Runs one conversion. Resolves to { name, version, bytes }; rejects with an Error whose
    // message is the reason the converter printed, and whose `code` is its status code.
    Module['extractRom'] = function (romBytes, options) {
        options = options || {};
        return new Promise(function (resolve, reject) {
            if (used) {
                reject(new Error('This converter instance has already run; create a new one per ROM.'));
                return;
            }
            used = true;
            var FS = Module['FS'];
            var errorLines = [];
            var onProgress = typeof options.onProgress === 'function' ? options.onProgress : null;

            Module['_sohExtractLine'] = function (line, isError) {
                if (isError) {
                    errorLines.push(line);
                    return false;
                }
                var m = PROGRESS_LINE.exec(line);
                if (m && onProgress) onProgress(Number(m[1]), Number(m[2]));
                return options.quiet === true;
            };

            var code;
            try {
                mkdirIfMissing('/rom');
                mkdirIfMissing(OUT_DIR);
                FS.writeFile(ROM_PATH, romBytes);
                code = Module['ccall']('Extract_RomToO2r', 'number', ['string', 'string'], [ROM_PATH, OUT_DIR]);
            } catch (e) {
                Module['_sohExtractLine'] = null;
                reject(e);
                return;
            }
            Module['_sohExtractLine'] = null;

            var result = JSON.parse(Module['ccall']('Extract_ResultJson', 'string'));
            try {
                FS.unlink(ROM_PATH);
            } catch (e) {}

            if (code !== 0) {
                var message = result.error || 'Extraction failed.';
                if (errorLines.length) message += '\n' + errorLines.slice(-20).join('\n');
                var error = new Error(message);
                error.code = code;
                reject(error);
                return;
            }

            var outPath = OUT_DIR + '/' + result.archive;
            var bytes = FS.readFile(outPath);
            try {
                FS.unlink(outPath);
            } catch (e) {}
            resolve({ name: result.archive, version: result.version, bytes: bytes });
        });
    };
})();
