// SOH [WASM] The host-facing side of the converter (appended to soh-extract.js as --post-js,
// so `Module` here is the instance the factory returns). The contract is in
// soh/wasm/HOST-API.md; keep this file free of any assumption about who is calling.
//
//   const mod = await createSohExtractor();
//   const { name, version, bytes } = await mod.extractRom(romBytes, { onProgress });
//
// onProgress(done, total, info) runs in two phases, each counting its own units:
//   info.phase === 'recipe'  done/total over ZAPD's recipe files; info.file names the one
//                            that is starting (ZAPD prints its "(i / N): path" line first).
//   info.phase === 'write'   done/total over the archive's entries while libzip writes it.

(function () {
    var ROM_PATH = '/rom/rom.z64';
    var OUT_DIR = '/out';
    var PROGRESS_LINE = /^\((\d+) \/ (\d+)\): /;
    var used = false;

    function mkdirIfMissing(path) {
        var FS = Module['FS'];
        if (!FS.analyzePath(path).exists) FS.mkdir(path);
    }

    function unlinkIfPresent(path) {
        try {
            Module['FS'].unlink(path);
        } catch (e) {}
    }

    // Runs one conversion. Resolves to { name, version, bytes }; rejects with an Error whose
    // message is the reason the converter printed, and whose `code` is its status code.
    Module['extractRom'] = function (romBytes, options) {
        options = options || {};
        return new Promise(function (resolve, reject) {
            if (used) {
                var reuse = new Error('This converter instance has already run; create a new one per ROM.');
                reuse.code = -8;
                reject(reuse);
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
                if (m && onProgress) {
                    onProgress(Number(m[1]), Number(m[2]), { phase: 'recipe', file: line.slice(m[0].length) });
                }
                return options.quiet === true;
            };
            Module['_sohExtractWrite'] = function (done, total) {
                if (onProgress) onProgress(done, total, { phase: 'write' });
            };

            function fail(message, code) {
                if (errorLines.length) message += '\n' + errorLines.slice(-20).join('\n');
                var error = new Error(message);
                error.code = code;
                reject(error);
            }

            // Everything that can throw is inside one funnel: a trap or an exception the C++
            // side did not catch, a result that will not parse, an archive that is not there.
            // Whatever happens, the hooks come down and the ROM leaves the VFS.
            var code, result, bytes, outPath;
            try {
                mkdirIfMissing('/rom');
                mkdirIfMissing(OUT_DIR);
                FS.writeFile(ROM_PATH, romBytes);
                code = Module['ccall']('Extract_RomToO2r', 'number', ['string', 'string'], [ROM_PATH, OUT_DIR]);
                result = JSON.parse(Module['ccall']('Extract_ResultJson', 'string'));
                if (code === 0) {
                    outPath = OUT_DIR + '/' + result.archive;
                    bytes = FS.readFile(outPath);
                }
            } catch (e) {
                fail('Extraction failed: ' + ((e && e.message) || String(e)), -6);
                return;
            } finally {
                Module['_sohExtractLine'] = null;
                Module['_sohExtractWrite'] = null;
                unlinkIfPresent(ROM_PATH);
            }

            if (code !== 0) {
                fail(result.error || 'Extraction failed.', code);
                return;
            }
            unlinkIfPresent(outPath);
            resolve({ name: result.archive, version: result.version, bytes: bytes });
        });
    };
})();
