// SOH [WASM] Runs before the runtime captures Module.print / Module.printErr, which is the
// only moment they can be intercepted: MODULARIZE binds them once, at startup. api.js
// installs a line handler here while a conversion runs (Torch's warnings and errors are
// ordinary stderr, its other log lines stdout) and leaves everything else to the host's own
// print functions, or the console.
(function () {
    var hostPrint = Module['print'] || console.log.bind(console);
    var hostPrintErr = Module['printErr'] || console.error.bind(console);
    Module['_sohExtractLine'] = null; // function (line, isError) -> true to swallow the line
    Module['print'] = function (line) {
        var handler = Module['_sohExtractLine'];
        if (handler && handler(line, false)) return;
        hostPrint(line);
    };
    Module['printErr'] = function (line) {
        var handler = Module['_sohExtractLine'];
        if (handler && handler(line, true)) return;
        hostPrintErr(line);
    };
})();
