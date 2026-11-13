// This code implements the `-sMODULARIZE` settings by taking the generated
// JS program code (INNER_JS_CODE) and wrapping it in a factory function.

// Single threaded MINIMAL_RUNTIME programs do not need access to
// document.currentScript, so a simple export declaration is enough.
var createUEModule = (() => {
  // When MODULARIZE this JS may be executed later,
  // after document.currentScript is gone, so we save it.
  // In EXPORT_ES6 mode we can just use 'import.meta.url'.
  var _scriptName = globalThis.document?.currentScript?.src;
  return async function(moduleArg = {}) {
    var Module = moduleArg;
// include: shell.js
// include: minimum_runtime_check.js
(function() {
  // "30.0.0" -> 300000
  function humanReadableVersionToPacked(str) {
    str = str.split("-")[0];
    // Remove any trailing part from e.g. "12.53.3-alpha"
    var vers = str.split(".").slice(0, 3);
    while (vers.length < 3) vers.push("00");
    vers = vers.map((n, i, arr) => n.padStart(2, "0"));
    return vers.join("");
  }
  // 300000 -> "30.0.0"
  var packedVersionToHumanReadable = n => [ n / 1e4 | 0, (n / 100 | 0) % 100, n % 100 ].join(".");
  var TARGET_NOT_SUPPORTED = 2147483647;
  // Note: We use a typeof check here instead of optional chaining using
  // globalThis because older browsers might not have globalThis defined.
  // We skip the node version checking when running on Bun/Deno since the node
  // version they report doesn't seem to be useful.
  if (typeof process !== "undefined" && !process.versions?.bun && typeof Deno == "undefined") {
    var currentNodeVersion = process.versions?.node ? humanReadableVersionToPacked(process.versions.node) : TARGET_NOT_SUPPORTED;
    if (currentNodeVersion < TARGET_NOT_SUPPORTED) {
      throw new Error("not compiled for this environment (did you build to HTML and try to run it not on the web, or set ENVIRONMENT to something - like node - and run it someplace else - like on the web?)");
    }
    if (currentNodeVersion < 2147483647) {
      throw new Error(`This emscripten-generated code requires node v${packedVersionToHumanReadable(2147483647)} (detected v${packedVersionToHumanReadable(currentNodeVersion)})`);
    }
  }
  var userAgent = typeof navigator !== "undefined" && navigator.userAgent;
  if (!userAgent) {
    return;
  }
  var currentSafariVersion = userAgent.includes("Safari/") && !userAgent.includes("Chrome/") && userAgent.match(/Version\/(\d+\.?\d*\.?\d*)/) ? humanReadableVersionToPacked(userAgent.match(/Version\/(\d+\.?\d*\.?\d*)/)[1]) : TARGET_NOT_SUPPORTED;
  if (currentSafariVersion < 15e4) {
    throw new Error(`This emscripten-generated code requires Safari v${packedVersionToHumanReadable(15e4)} (detected v${currentSafariVersion})`);
  }
  var currentFirefoxVersion = userAgent.match(/Firefox\/(\d+(?:\.\d+)?)/) ? parseFloat(userAgent.match(/Firefox\/(\d+(?:\.\d+)?)/)[1]) : TARGET_NOT_SUPPORTED;
  if (currentFirefoxVersion < 79) {
    throw new Error(`This emscripten-generated code requires Firefox v79 (detected v${currentFirefoxVersion})`);
  }
  var currentChromeVersion = userAgent.match(/Chrome\/(\d+(?:\.\d+)?)/) ? parseFloat(userAgent.match(/Chrome\/(\d+(?:\.\d+)?)/)[1]) : TARGET_NOT_SUPPORTED;
  if (currentChromeVersion < 85) {
    throw new Error(`This emscripten-generated code requires Chrome v85 (detected v${currentChromeVersion})`);
  }
})();

// end include: minimum_runtime_check.js
// The Module object: Our interface to the outside world. We import
// and export values on it. There are various ways Module can be used:
// 1. Not defined. We create it here
// 2. A function parameter, function(moduleArg) => Promise<Module>
// 3. pre-run appended it, var Module = {}; ..generated code..
// 4. External script tag defines var Module.
// We need to check if Module already exists (e.g. case 3 above).
// Substitution will be replaced with actual code on later stage of the build,
// this way Closure Compiler will not mangle it (e.g. case 4. above).
// Note that if you want to run closure, and also to use Module
// after the generated code, you will need to define   var Module = {};
// before the code. Then that object will be used in the code, and you
// can continue to use Module afterwards as well.
// Determine the runtime environment we are in. You can customize this by
// setting the ENVIRONMENT setting at compile time (see settings.js).
// Attempt to auto-detect the environment
var ENVIRONMENT_IS_WEB = !!globalThis.window;

var ENVIRONMENT_IS_WORKER = !!globalThis.WorkerGlobalScope;

// N.b. Electron.js environment is simultaneously a NODE-environment, but
// also a web environment.
var ENVIRONMENT_IS_NODE = globalThis.process?.versions?.node && globalThis.process?.type != "renderer";

var ENVIRONMENT_IS_SHELL = !ENVIRONMENT_IS_WEB && !ENVIRONMENT_IS_NODE && !ENVIRONMENT_IS_WORKER;

// Three configurations we can be running in:
// 1) We could be the application main() thread running in the main JS UI thread. (ENVIRONMENT_IS_WORKER == false and ENVIRONMENT_IS_PTHREAD == false)
// 2) We could be the application main() running directly in a worker. (ENVIRONMENT_IS_WORKER == true, ENVIRONMENT_IS_PTHREAD == false)
// 3) We could be an application pthread running in a worker. (ENVIRONMENT_IS_WORKER == true and ENVIRONMENT_IS_PTHREAD == true)
// The way we signal to a worker that it is hosting a pthread is to construct
// it with a specific name.
var ENVIRONMENT_IS_PTHREAD = ENVIRONMENT_IS_WORKER && globalThis.name?.startsWith("em-pthread");

if (ENVIRONMENT_IS_PTHREAD) {
  assert(!globalThis.moduleLoaded, "module should only be loaded once on each pthread worker");
  globalThis.moduleLoaded = true;
}

// --pre-jses are emitted after the Module integration code, so that they can
// refer to Module (if they choose; they can also define Module)
var programArgs = [];

var thisProgram = "./this.program";

var quit_ = (status, toThrow) => {
  throw toThrow;
};

if (ENVIRONMENT_IS_WORKER) {
  _scriptName = self.location.href;
}

// `/` should be present at the end if `scriptDirectory` is not empty
var scriptDirectory = "";

function locateFile(path) {
  if (Module["locateFile"]) {
    return Module["locateFile"](path, scriptDirectory);
  }
  return scriptDirectory + path;
}

// Hooks that are implemented differently in different runtime environments.
var readAsync, readBinary;

if (ENVIRONMENT_IS_SHELL) {} else // Note that this includes Node.js workers when relevant (pthreads is enabled).
// Node.js workers are detected as a combination of ENVIRONMENT_IS_WORKER and
// ENVIRONMENT_IS_NODE.
if (ENVIRONMENT_IS_WEB || ENVIRONMENT_IS_WORKER) {
  try {
    scriptDirectory = new URL(".", _scriptName).href;
  } catch {}
  if (!(globalThis.window || globalThis.WorkerGlobalScope)) throw new Error("not compiled for this environment (did you build to HTML and try to run it not on the web, or set ENVIRONMENT to something - like node - and run it someplace else - like on the web?)");
  {
    // include: web_or_worker_shell_read.js
    if (ENVIRONMENT_IS_WORKER) {
      readBinary = url => {
        var xhr = new XMLHttpRequest;
        xhr.open("GET", url, false);
        xhr.responseType = "arraybuffer";
        xhr.send(null);
        return new Uint8Array(/** @type{!ArrayBuffer} */ (xhr.response));
      };
    }
    readAsync = async url => {
      assert(!isFileURI(url), "readAsync does not work with file:// URLs");
      var response = await fetch(url, {
        credentials: "same-origin"
      });
      if (response.ok) {
        return response.arrayBuffer();
      }
      throw new Error(response.status + " : " + response.url);
    };
  }
} else {
  throw new Error("environment detection error");
}

var out = console.log.bind(console);

var err = console.error.bind(console);

var IDBFS = "IDBFS is no longer included by default; build with -lidbfs.js";

var PROXYFS = "PROXYFS is no longer included by default; build with -lproxyfs.js";

var WORKERFS = "WORKERFS is no longer included by default; build with -lworkerfs.js";

var FETCHFS = "FETCHFS is no longer included by default; build with -lfetchfs.js";

var ICASEFS = "ICASEFS is no longer included by default; build with -licasefs.js";

var JSFILEFS = "JSFILEFS is no longer included by default; build with -ljsfilefs.js";

var OPFS = "OPFS is no longer included by default; build with -lopfs.js";

var NODEFS = "NODEFS is no longer included by default; build with -lnodefs.js";

// perform assertions in shell.js after we set up out() and err(), as otherwise
// if an assertion fails it cannot print the message
assert(ENVIRONMENT_IS_WEB || ENVIRONMENT_IS_WORKER || ENVIRONMENT_IS_NODE, "pthreads do not work in this environment yet (need Web Workers, or an alternative to them)");

assert(!ENVIRONMENT_IS_NODE, "node environment detected but not enabled at build time (add `node` to `-sENVIRONMENT` to enable)");

assert(!ENVIRONMENT_IS_SHELL, "shell environment detected but not enabled at build time (add `shell` to `-sENVIRONMENT` to enable)");

// end include: shell.js
// include: preamble.js
// === Preamble library stuff ===
// Documentation for the public APIs defined in this file must be updated in:
//    site/source/docs/api_reference/preamble.js.rst
// A prebuilt local version of the documentation is available at:
//    site/build/text/docs/api_reference/preamble.js.txt
// You can also build docs locally as HTML or other formats in site/
// An online HTML version (which may be of a different version of Emscripten)
//    is up at http://kripken.github.io/emscripten-site/docs/api_reference/preamble.js.html
var wasmBinary;

if (!globalThis.WebAssembly) {
  err("no native wasm support detected");
}

// Wasm globals
// For sending to workers.
var wasmModule;

//========================================
// Runtime essentials
//========================================
// whether we are quitting the application. no code should run after this.
// set in exit() and abort()
var ABORT = false;

// set by exit() and abort().  Passed to 'onExit' handler.
// NOTE: This is also used as the process return code in shell environments
// but only when noExitRuntime is false.
var EXITSTATUS;

// In STRICT mode, we only define assert() when ASSERTIONS is set.  i.e. we
// don't define it at all in release modes.  This matches the behaviour of
// MINIMAL_RUNTIME.
// TODO(sbc): Make this the default even without STRICT enabled.
/** @type {function(*, string=)} */ function assert(condition, text) {
  if (!condition) {
    abort("Assertion failed" + (text ? ": " + text : ""));
  }
}

// We used to include malloc/free by default in the past. Show a helpful error in
// builds with assertions.
/**
 * Indicates whether filename is delivered via file protocol (as opposed to http/https)
 * @noinline
 */ var isFileURI = filename => filename.startsWith("file://");

// include: runtime_common.js
// include: runtime_exceptions.js
// Base Emscripten EH error class
class EmscriptenEH extends Error {}

class EmscriptenSjLj extends EmscriptenEH {}

class CppException extends EmscriptenEH {
  constructor(excPtr) {
    super(excPtr);
    this.excPtr = excPtr;
    const excInfo = getExceptionMessage(this);
    this.name = excInfo[0];
    this.message = excInfo[1];
  }
}

// end include: runtime_exceptions.js
// include: runtime_debug.js
var runtimeDebug = true;

// Switch to false at runtime to disable logging at the right times
// Used by XXXXX_DEBUG settings to output debug messages.
function dbg(...args) {
  if (!runtimeDebug && typeof runtimeDebug != "undefined") return;
  // TODO(sbc): Make this configurable somehow.  Its not always convenient for
  // logging to show up as warnings.
  console.warn(...args);
}

// Endianness check
(() => {
  var h16 = new Int16Array(1);
  var h8 = new Int8Array(h16.buffer);
  h16[0] = 25459;
  if (h8[0] !== 115 || h8[1] !== 99) abort("Runtime error: expected the system to be little-endian! (Run with -sSUPPORT_BIG_ENDIAN to bypass)");
})();

function consumedModuleProp(prop) {
  var value = Module[prop];
  var msg = `Attempt to modify \`Module.${prop}\` after it has already been processed.  This can happen, for example, when code is injected via '--post-js' rather than '--pre-js'`;
  if (Array.isArray(value)) {
    value = new Proxy(value, {
      set(target, key, val) {
        abort(msg);
        return false;
      },
      defineProperty(target, key, descriptor) {
        abort(msg);
        return false;
      },
      deleteProperty(target, key) {
        abort(msg);
        return false;
      }
    });
  }
  Object.defineProperty(Module, prop, {
    configurable: true,
    get() {
      return value;
    },
    set() {
      abort(msg);
    }
  });
}

function makeInvalidEarlyAccess(name) {
  return () => assert(false, `call to '${name}' via reference taken before Wasm module initialization`);
}

function ignoredModuleProp(prop) {
  if (Object.getOwnPropertyDescriptor(Module, prop)) {
    abort(`\`Module.${prop}\` was supplied but \`${prop}\` not included in INCOMING_MODULE_JS_API`);
  }
}

// forcing the filesystem exports a few things by default
function isExportedByForceFilesystem(name) {
  return name === "FS_createPath" || name === "FS_createDataFile" || name === "FS_createPreloadedFile" || name === "FS_preloadFile" || name === "FS_unlink" || name === "addRunDependency" || name === "removeRunDependency";
}

function missingLibrarySymbol(sym) {
  // Any symbol that is not included from the JS library is also (by definition)
  // not exported on the Module object.
  unexportedRuntimeSymbol(sym);
}

function unexportedRuntimeSymbol(sym) {
  if (ENVIRONMENT_IS_PTHREAD) {
    return;
  }
  if (!Object.getOwnPropertyDescriptor(Module, sym)) {
    Object.defineProperty(Module, sym, {
      configurable: true,
      get() {
        var msg = `'${sym}' was not exported. add it to EXPORTED_RUNTIME_METHODS (see the Emscripten FAQ)`;
        if (isExportedByForceFilesystem(sym)) {
          msg += ". Alternatively, forcing filesystem support (-sFORCE_FILESYSTEM) can export this for you";
        }
        abort(msg);
      }
    });
  }
}

/**
 * Override `err`/`out`/`dbg` to report thread / worker information
 */ function initWorkerLogging() {
  function getLogPrefix() {
    var t = 0;
    if (runtimeInitialized && typeof _pthread_self != "undefined" && !runtimeExited) {
      t = _pthread_self();
    }
    return `w:${workerID},t:${ptrToString(t)}:`;
  }
  // Prefix all dbg() messages with the calling thread info.
  var origDbg = dbg;
  dbg = (...args) => origDbg(getLogPrefix(), ...args);
}

initWorkerLogging();

// end include: runtime_debug.js
// include: runtime_stack_check.js
const stackCookie1 = 34821223;

const stackCookie2 = 2310721022;

// Initializes the stack cookie. Called at the startup of main and at the startup of each thread in pthreads mode.
function writeStackCookie() {
  var max = _emscripten_stack_get_end();
  assert((max & 3) == 0);
  // If the stack ends at address zero we write our cookies 4 bytes into the
  // stack.  This prevents interference with SAFE_HEAP and ASAN which also
  // monitor writes to address zero.
  if (max == 0) {
    max += 4;
  }
  // The stack grow downwards towards _emscripten_stack_get_end.
  // We write cookies to the final two words in the stack and detect if they are
  // ever overwritten.
  (growMemViews(), HEAPU32)[((max) >> 2)] = stackCookie1;
  (growMemViews(), HEAPU32)[(((max) + (4)) >> 2)] = stackCookie2;
  // Also test the global address 0 for integrity.
  (growMemViews(), HEAPU32)[((0) >> 2)] = 1668509029;
}

function u32ToHexString(num) {
  return "0x" + (num >>> 0).toString(16).padStart(8, "0");
}

function checkStackCookie() {
  if (ABORT) return;
  var max = _emscripten_stack_get_end();
  // See writeStackCookie().
  if (max == 0) {
    max += 4;
  }
  var val1 = (growMemViews(), HEAPU32)[((max) >> 2)];
  var val2 = (growMemViews(), HEAPU32)[(((max) + (4)) >> 2)];
  if (val1 != stackCookie1 || val2 != stackCookie2) {
    abort(`Stack overflow! Stack cookie has been overwritten at ${ptrToString(max)}, expected hex dwords ${u32ToHexString(stackCookie2)} and ${u32ToHexString(stackCookie1)}, but received ${u32ToHexString(val2)} ${u32ToHexString(val1)}`);
  }
  // Also test the global address 0 for integrity.
  if ((growMemViews(), HEAPU32)[((0) >> 2)] != 1668509029) {
    abort("Runtime error: The application has corrupted its heap memory area (address zero)!");
  }
}

// end include: runtime_stack_check.js
// Support for growable heap + pthreads, where the buffer may change, so JS views
// must be updated.
function growMemViews() {
  // `updateMemoryViews` updates all the views simultaneously, so it's enough to check any of them.
  if (wasmMemory.buffer != HEAP8.buffer) {
    updateMemoryViews();
  }
}

// include: runtime_pthread.js
// Pthread Web Worker handling code.
// This code runs only on pthread web workers and handles pthread setup
// and communication with the main thread via postMessage.
// Unique ID of the current pthread worker (zero on non-pthread-workers
// including the main thread).
var workerID = 0;

var startWorker;

if (ENVIRONMENT_IS_PTHREAD) {
  // Thread-local guard variable for one-time init of the JS state
  var initializedJS = false;
  // Turn unhandled rejected promises into errors so that the main thread will be
  // notified about them.
  self.onunhandledrejection = e => {
    throw e.reason || e;
  };
  function handleMessage(e) {
    try {
      var msgData = e.data;
      //dbg('msgData: ' + Object.keys(msgData));
      var cmd = msgData.cmd;
      if (cmd == 1) {
        // Preload command that is called once per worker to parse and load the Emscripten code.
        workerID = msgData.workerID;
        // Until we initialize the runtime, queue up any further incoming messages.
        let messageQueue = [];
        self.onmessage = e => messageQueue.push(e);
        // And add a callback for when the runtime is initialized.
        startWorker = () => {
          // Notify the main thread that this thread has loaded.
          postMessage({
            cmd: 3
          });
          // Process any messages that were queued before the thread was ready.
          for (let msg of messageQueue) {
            handleMessage(msg);
          }
          // Restore the real message handler.
          self.onmessage = handleMessage;
        };
        // Use `const` here to ensure that the variable is scoped only to
        // that iteration, allowing safe reference from a closure.
        for (const handler of msgData.handlers) {
          // If the main module has a handler for a certain event, but no
          // handler exists on the pthread worker, then proxy that handler
          // back to the main thread.
          if (!Module[handler] || Module[handler].proxy) {
            Module[handler] = (...args) => {
              postMessage({
                cmd: 9,
                handler,
                args
              });
            };
            // Rebind the out / err handlers if needed
            if (handler == "print") out = Module[handler];
            if (handler == "printErr") err = Module[handler];
          }
        }
        wasmMemory = msgData.wasmMemory;
        updateMemoryViews();
        wasmModule = msgData.wasmModule;
        createWasm();
        run();
        startWorker();
      } else if (cmd == 2) {
        assert(msgData.pthread_ptr);
        assert(wasmMemory, "CMD_RUN received before CMD_LOAD");
        // Call inside JS module to set up the stack frame for this pthread in JS module scope.
        // This needs to be the first thing that we do, as we cannot call to any C/C++ functions
        // until the thread stack is initialized.
        establishStackSpace(msgData.pthread_ptr);
        // Pass the thread address to wasm to store it for fast access.
        __emscripten_thread_init(msgData.pthread_ptr, /*is_main=*/ 0, /*is_runtime=*/ 0, /*can_block=*/ 1, 0, 0);
        PThread.threadInitTLS();
        // Await mailbox notifications with `Atomics.waitAsync` so we can start
        // using the fast `Atomics.notify` notification path.
        __emscripten_thread_mailbox_await(msgData.pthread_ptr);
        if (!initializedJS) {
          initializedJS = true;
        }
        try {
          invokeEntryPoint(msgData.start_routine, msgData.arg);
        } catch (ex) {
          if (ex != "unwind") {
            // The pthread "crashed".  Do not call `_emscripten_thread_exit` (which
            // would make this thread joinable).  Instead, re-throw the exception
            // and let the top level handler propagate it back to the main thread.
            throw ex;
          }
        }
      } else if (cmd == 4) {
        if (initializedJS) {
          checkMailbox();
        }
      } else if (cmd) {
        // The received message looks like something that should be handled by this message
        // handler, (since there is a cmd field present), but is not one of the
        // recognized commands:
        err(`worker: received unknown command ${cmd}`);
        err(msgData);
      }
    } catch (ex) {
      err(`worker: onmessage() captured an uncaught exception: ${ex}`);
      if (ex?.stack) err(ex.stack);
      if (runtimeInitialized) __emscripten_thread_crashed();
      throw ex;
    }
  }
  self.onmessage = handleMessage;
}

// ENVIRONMENT_IS_PTHREAD
// end include: runtime_pthread.js
// Memory management
var runtimeInitialized = false;

var runtimeExited = false;

// When ALLOW_MEMORY_GROWTH is enabled, the conversion from Wasm
// memory to ArrayBuffer requires some additional logic.
function getMemoryBuffer() {
  return wasmMemory.buffer;
}

function updateMemoryViews() {
  // If we already have a heap that is resizeable/growable buffer we don't
  // need to do anything in updateMemoryViews.
  if (HEAP8?.buffer?.growable) return;
  var b = getMemoryBuffer();
  HEAP8 = new Int8Array(b);
  HEAP16 = new Int16Array(b);
  HEAPU8 = new Uint8Array(b);
  HEAPU16 = new Uint16Array(b);
  HEAP32 = new Int32Array(b);
  HEAPU32 = new Uint32Array(b);
  HEAPF32 = new Float32Array(b);
  HEAPF64 = new Float64Array(b);
  HEAP64 = new BigInt64Array(b);
  HEAPU64 = new BigUint64Array(b);
}

// In non-standalone/normal mode, we create the memory here.
// include: runtime_init_memory.js
// Create the wasm memory. (Note: this only applies if IMPORTED_MEMORY is defined)
// check for full engine support (use string 'subarray' to avoid closure compiler confusion)
function initMemory() {
  if ((ENVIRONMENT_IS_PTHREAD)) {
    return;
  }
  if (Module["wasmMemory"]) {
    wasmMemory = Module["wasmMemory"];
  } else {
    var INITIAL_MEMORY = 536870912;
    assert(INITIAL_MEMORY >= 4194304, `INITIAL_MEMORY should be larger than STACK_SIZE, was ${INITIAL_MEMORY}! (STACK_SIZE=4194304)`);
    /** @suppress {checkTypes} */ wasmMemory = new WebAssembly.Memory({
      "initial": INITIAL_MEMORY / 65536,
      // In theory we should not need to emit the maximum if we want "unlimited"
      // or 4GB of memory, but VMs error on that atm, see
      // https://github.com/emscripten-core/emscripten/issues/14130
      // And in the pthreads case we definitely need to emit a maximum. So
      // always emit one.
      "maximum": 65536,
      "shared": true
    });
  }
  updateMemoryViews();
}

// end include: runtime_init_memory.js
// include: memoryprofiler.js
// end include: memoryprofiler.js
// end include: runtime_common.js
assert(globalThis.Int32Array && globalThis.Float64Array && Int32Array.prototype.subarray && Int32Array.prototype.set, "JS engine does not provide full typed array support");

function preRun() {
  assert(!ENVIRONMENT_IS_PTHREAD);
  // PThreads reuse the runtime from the main thread.
  var preRun = Module["preRun"];
  if (preRun) {
    if (typeof preRun == "function") preRun = [ preRun ];
    onPreRuns.push(...preRun);
  }
  consumedModuleProp("preRun");
  // Begin ATPRERUNS hooks
  callRuntimeCallbacks(onPreRuns);
}

function initRuntime() {
  assert(!runtimeInitialized);
  runtimeInitialized = true;
  if (ENVIRONMENT_IS_PTHREAD) return;
  checkStackCookie();
  // No ATINITS hooks
  wasmExports["__wasm_call_ctors"]();
  // No ATPOSTCTORS hooks
  checkStackCookie();
}

var runtimeExiting = false;

function exitRuntime() {
  assert(!runtimeExited);
  assert(!runtimeExiting, "Re-entrant call to exitRuntime()! This can happen if an atexit() registered callback throws an exception.");
  runtimeExiting = true;
  assert(!(ENVIRONMENT_IS_PTHREAD), "exitRuntime() should only be called from the main thread");
  checkStackCookie();
  ___funcs_on_exit();
  // Native atexit() functions
  // Begin ATEXITS hooks
  callRuntimeCallbacks(onExits);
  // End ATEXITS hooks
  PThread.terminateRuntime();
  runtimeExited = true;
}

function postRun() {
  checkStackCookie();
  var postRun = Module["postRun"];
  if (postRun) {
    if (typeof postRun == "function") postRun = [ postRun ];
    onPostRuns.push(...postRun);
  }
  consumedModuleProp("postRun");
  // Begin ATPOSTRUNS hooks
  callRuntimeCallbacks(onPostRuns);
}

/**
 * @param {string|number=} what
 */ function abort(what) {
  Module["onAbort"]?.(what);
  what = `Aborted(${what})`;
  // TODO(sbc): Should we remove printing and leave it up to whoever
  // catches the exception?
  err(what);
  ABORT = true;
  // Use a wasm runtime error, because a JS error might be seen as a foreign
  // exception, which means we'd run destructors on it. We need the error to
  // simply make the program stop.
  // FIXME This approach does not work in Wasm EH because it currently does not assume
  // all RuntimeErrors are from traps; it decides whether a RuntimeError is from
  // a trap or not based on a hidden field within the object. So at the moment
  // we don't have a way of throwing a wasm trap from JS. TODO Make a JS API that
  // allows this in the wasm spec.
  // Suppress closure compiler warning here. Closure compiler's builtin extern
  // definition for WebAssembly.RuntimeError claims it takes no arguments even
  // though it can.
  // TODO(https://github.com/google/closure-compiler/pull/3913): Remove if/when upstream closure gets fixed.
  /** @suppress {checkTypes} */ var e = new WebAssembly.RuntimeError(what);
  // Throw the error whether or not MODULARIZE is set because abort is used
  // in code paths apart from instantiation where an exception is expected
  // to be thrown when abort is called.
  throw e;
}

function createExportWrapper(name, func, nargs) {
  assert(func);
  return (...args) => {
    assert(runtimeInitialized, `native function \`${name}\` called before runtime initialization`);
    assert(!runtimeExited, `native function \`${name}\` called after runtime exit (use NO_EXIT_RUNTIME to keep it alive after main() exits)`);
    // Only assert for too many arguments. Too few can be valid since the missing arguments will be zero filled.
    assert(args.length <= nargs, `native function \`${name}\` called with ${args.length} args but expects ${nargs}`);
    return func(...args);
  };
}

var wasmBinaryFile;

function findWasmBinary() {
  return locateFile("Phoenix.wasm");
}

function getBinarySync(file) {
  if (readBinary) {
    return readBinary(file);
  }
  // Throwing a plain string here, even though it not normally advisable since
  // this gets turning into an `abort` in instantiateArrayBuffer.
  throw "both async and sync fetching of the wasm failed";
}

async function getWasmBinary(binaryFile) {
  // If we don't have the binary yet, load it asynchronously using readAsync.
  if (!wasmBinary) {
    // Fetch the binary using readAsync
    try {
      var response = await readAsync(binaryFile);
      return new Uint8Array(response);
    } catch {}
  }
  // Otherwise, getBinarySync should be able to get it synchronously
  return getBinarySync(binaryFile);
}

async function instantiateArrayBuffer(binaryFile, imports) {
  try {
    var binary = await getWasmBinary(binaryFile);
    var instance = await WebAssembly.instantiate(binary, imports);
    return instance;
  } catch (reason) {
    err(`failed to asynchronously prepare wasm: ${reason}`);
    // Warn on some common problems.
    if (isFileURI(binaryFile)) {
      err(`warning: Loading from a file URI (${binaryFile}) is not supported in most browsers. See https://emscripten.org/docs/getting_started/FAQ.html#how-do-i-run-a-local-webserver-for-testing-why-does-my-program-stall-in-downloading-or-preparing`);
    }
    abort(reason);
  }
}

async function instantiateAsync(binary, binaryFile, imports) {
  if (!binary) {
    try {
      var response = fetch(binaryFile, {
        credentials: "same-origin"
      });
      var instantiationResult = await WebAssembly.instantiateStreaming(response, imports);
      return instantiationResult;
    } catch (reason) {
      // We expect the most common failure cause to be a bad MIME type for the binary,
      // in which case falling back to ArrayBuffer instantiation should work.
      err(`wasm streaming compile failed: ${reason}`);
      err("falling back to ArrayBuffer instantiation");
    }
  }
  return instantiateArrayBuffer(binaryFile, imports);
}

function getWasmImports() {
  assignWasmImports();
  // prepare imports
  var imports = {
    "env": wasmImports,
    "wasi_snapshot_preview1": wasmImports
  };
  return imports;
}

// Create the wasm instance.
// Receives the wasm imports, returns the exports.
async function createWasm() {
  // Load the wasm module and create an instance of using native support in the JS engine.
  // handle a generated wasm instance, receiving its exports and
  // performing other necessary setup
  function receiveInstance(instance, module) {
    wasmExports = instance.exports;
    wasmExports = applySignatureConversions(wasmExports);
    registerTLSInit(wasmExports["_emscripten_tls_init"]);
    assignWasmExports(wasmExports);
    // We now have the Wasm module loaded up, keep a reference to the compiled module so we can post it to the workers.
    wasmModule = module;
    return wasmExports;
  }
  // Prefer streaming instantiation if available.
  // Async compilation can be confusing when an error on the page overwrites Module
  // (for example, if the order of elements is wrong, and the one defining Module is
  // later), so we save Module and check it later.
  var trueModule = Module;
  function receiveInstantiationResult(result) {
    // 'result' is a ResultObject object which has both the module and instance.
    // receiveInstance() will swap in the exports (to Module.asm) so they can be called
    assert(Module === trueModule, "the Module object should not be replaced during async compilation - perhaps the order of HTML elements is wrong?");
    trueModule = null;
    return receiveInstance(result["instance"], result["module"]);
  }
  var info = getWasmImports();
  // User shell pages can write their own Module.instantiateWasm = function(imports, successCallback) callback
  // to manually instantiate the Wasm module themselves. This allows pages to
  // run the instantiation parallel to any other async startup actions they are
  // performing.
  // Also pthreads and wasm workers initialize the wasm instance through this
  // path.
  var instantiateWasm = Module["instantiateWasm"];
  if (instantiateWasm) {
    return new Promise(resolve => {
      try {
        instantiateWasm(info, (inst, mod) => resolve(receiveInstance(inst, mod)));
      } catch (e) {
        err(`Module.instantiateWasm callback failed with error: ${e}`);
        throw e;
      }
    });
  }
  if ((ENVIRONMENT_IS_PTHREAD)) {
    // Instantiate from the module that was received via postMessage from
    // the main thread. We can just use sync instantiation in the worker.
    assert(wasmModule, "wasmModule should have been received via postMessage");
    var instance = new WebAssembly.Instance(wasmModule, getWasmImports());
    return receiveInstance(instance, wasmModule);
  }
  wasmBinaryFile ??= findWasmBinary();
  var result = await instantiateAsync(wasmBinary, wasmBinaryFile, info);
  var exports = receiveInstantiationResult(result);
  return exports;
}

// end include: preamble.js
// Begin JS library code
class ExitStatus {
  name="ExitStatus";
  constructor(status) {
    this.message = `Program terminated with exit(${status})`;
    this.status = status;
  }
}

/** @type {!Int32Array} */ var HEAP32;

/** @type {!Int8Array} */ var HEAP8;

/** @type {!Uint32Array} */ var HEAPU32;

var terminateWorker = worker => {
  worker.terminate();
  // terminate() can be asynchronous, so in theory the worker can continue
  // to run for some amount of time after termination.  However from our POV
  // the worker is now dead and we don't want to hear from it again, so we stub
  // out its message handler here.  This avoids having to check in each of
  // the onmessage handlers if the message was coming from a valid worker.
  worker.onmessage = e => {
    var cmd = e.data.cmd;
    err(`received "${cmd}" command from terminated worker: ${worker.workerID}`);
  };
};

var cleanupThread = pthread_ptr => {
  assert(!ENVIRONMENT_IS_PTHREAD, "cleanupThread() should only be called from the main thread");
  assert(pthread_ptr, "null pthread_ptr passed to cleanupThread");
  var worker = PThread.pthreads[pthread_ptr];
  assert(worker);
  PThread.returnWorkerToPool(worker);
};

var callRuntimeCallbacks = callbacks => {
  while (callbacks.length > 0) {
    // Pass the module as the first argument.
    callbacks.shift()(Module);
  }
};

var onPreRuns = [];

var addOnPreRun = cb => onPreRuns.push(cb);

var dependenciesPromise = null;

var resolveRunDependencies = async () => dependenciesPromise;

var runDependencies = 0;

var dependenciesPromiseResolve = null;

var runDependencyTracking = {};

var runDependencyWatcher = null;

var removeRunDependency = id => {
  runDependencies--;
  Module["monitorRunDependencies"]?.(runDependencies);
  assert(id, "removeRunDependency requires an ID");
  assert(runDependencyTracking[id]);
  delete runDependencyTracking[id];
  if (!runDependencies) {
    if (runDependencyWatcher !== null) {
      clearInterval(runDependencyWatcher);
      runDependencyWatcher = null;
    }
    dependenciesPromiseResolve();
  }
};

var addRunDependency = id => {
  if (!runDependencies) {
    dependenciesPromise = new Promise(resolve => dependenciesPromiseResolve = resolve);
  }
  runDependencies++;
  Module["monitorRunDependencies"]?.(runDependencies);
  assert(id, "addRunDependency requires an ID");
  assert(!runDependencyTracking[id]);
  runDependencyTracking[id] = 1;
  if (!runDependencyWatcher && globalThis.setInterval) {
    // Check for missing dependencies every few seconds
    runDependencyWatcher = setInterval(() => {
      if (ABORT) {
        clearInterval(runDependencyWatcher);
        runDependencyWatcher = null;
        return;
      }
      var shown = false;
      for (var dep in runDependencyTracking) {
        if (!shown) {
          shown = true;
          err("still waiting on run dependencies:");
        }
        err(`dependency: ${dep}`);
      }
      if (shown) {
        err("(end of list)");
      }
    }, 1e4);
  }
};

var spawnThread = threadParams => {
  assert(!ENVIRONMENT_IS_PTHREAD, "spawnThread() should only be called from the main thread");
  assert(threadParams.pthread_ptr, "spawnThread called with null pthread ptr");
  var worker = PThread.getNewWorker();
  if (!worker) {
    // No available workers in the PThread pool.
    return 6;
  }
  assert(!worker.pthread_ptr);
  // Add to pthreads map
  PThread.pthreads[threadParams.pthread_ptr] = worker;
  worker.pthread_ptr = threadParams.pthread_ptr;
  var msg = {
    cmd: 2,
    start_routine: threadParams.startRoutine,
    arg: threadParams.arg,
    pthread_ptr: threadParams.pthread_ptr
  };
  // Ask the worker to start executing its pthread entry point function.
  worker.postMessage(msg, threadParams.transferList);
  return 0;
};

var runtimeKeepaliveCounter = 0;

var keepRuntimeAlive = () => noExitRuntime || runtimeKeepaliveCounter > 0;

var stackSave = () => _emscripten_stack_get_current();

var stackRestore = val => __emscripten_stack_restore(val);

var stackAlloc = sz => __emscripten_stack_alloc(sz);

/** @type {!Float64Array} */ var HEAPF64;

/** not-@type {!BigInt64Array} */ var HEAP64;

/** @type{function(number, (number|boolean), ...number)} */ var proxyToMainThread = (funcIndex, emAsmAddr, proxyMode, ...callArgs) => {
  // EM_ASM proxying is done by passing a pointer to the address of the EM_ASM
  // content as `emAsmAddr`.  JS library proxying is done by passing an index
  // into `proxiedJSCallArgs` as `funcIndex`. If `emAsmAddr` is non-zero then
  // `funcIndex` will be ignored.
  // Additional arguments are passed after the first three are the actual
  // function arguments.
  // The serialization buffer contains the number of call params, and then
  // all the args here.
  // We also pass 'proxyMode' to C separately, since C needs to look at it.
  // Allocate a buffer (on the stack), which will be copied if necessary by
  // the C code.
  // First passed parameter specifies the number of arguments to the function.
  // When BigInt support is enabled, we must handle types in a more complex
  // way, detecting at runtime if a value is a BigInt or not (as we have no
  // type info here). To do that, add a "prefix" before each value that
  // indicates if it is a BigInt, which effectively doubles the number of
  // values we serialize for proxying. TODO: pack this?
  var bufSize = 8 * callArgs.length * 2;
  var sp = stackSave();
  var args = stackAlloc(bufSize);
  var b = ((args) >> 3);
  for (var arg of callArgs) {
    if (typeof arg == "bigint") {
      // The prefix is non-zero to indicate a bigint.
      (growMemViews(), HEAP64)[b++] = 1n;
      (growMemViews(), HEAP64)[b++] = arg;
    } else {
      // The prefix is zero to indicate a JS Number.
      (growMemViews(), HEAP64)[b++] = 0n;
      (growMemViews(), HEAPF64)[b++] = arg;
    }
  }
  var rtn = __emscripten_run_js_on_main_thread(funcIndex, emAsmAddr, bufSize, args, proxyMode);
  stackRestore(sp);
  return rtn;
};

function _proc_exit(code) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(0, 0, 1, code);
  EXITSTATUS = code;
  if (!keepRuntimeAlive()) {
    PThread.terminateAllThreads();
    Module["onExit"]?.(code);
    ABORT = true;
  }
  quit_(code, new ExitStatus(code));
}

var runtimeKeepalivePop = () => {
  assert(runtimeKeepaliveCounter > 0);
  runtimeKeepaliveCounter -= 1;
};

function exitOnMainThread(returnCode) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(1, 0, 0, returnCode);
  runtimeKeepalivePop();
  _exit(returnCode);
}

/** @param {boolean|number=} implicit */ var exitJS = (status, implicit) => {
  EXITSTATUS = status;
  if (ENVIRONMENT_IS_PTHREAD) {
    // implicit exit can never happen on a pthread
    assert(!implicit);
    // When running in a pthread we propagate the exit back to the main thread
    // where it can decide if the whole process should be shut down or not.
    // The pthread may have decided not to exit its own runtime, for example
    // because it runs a main loop, but that doesn't affect the main thread.
    exitOnMainThread(status);
    throw "unwind";
  }
  if (!keepRuntimeAlive()) {
    exitRuntime();
  }
  // if exit() was called explicitly, warn the user if the runtime isn't actually being shut down
  if (keepRuntimeAlive() && !implicit) {
    var msg = `program exited (with status: ${status}), but keepRuntimeAlive() is set (counter=${runtimeKeepaliveCounter}) due to an async operation, so halting execution but not exiting the runtime or preventing further async execution (you can use emscripten_force_exit, if you want to force a true shutdown)`;
    err(msg);
  }
  _proc_exit(status);
};

var _exit = exitJS;

var waitAsyncPolyfilled = (!Atomics.waitAsync || (globalThis.navigator?.userAgent && Number((navigator.userAgent.match(/Chrom(e|ium)\/([0-9]+)\./) || [])[2]) < 91));

function ptrToString(ptr) {
  assert(typeof ptr === "number", `ptrToString expects a number, got ${typeof ptr}`);
  // Convert to 64-bit unsigned value.  We need to use BigInt here since
  // Number cannot represent the full 64-bit range.
  if (ptr < 0) ptr = 2n ** 64n + BigInt(ptr);
  return "0x" + ptr.toString(16).padStart(16, "0");
}

var PThread = {
  unusedWorkers: [],
  tlsInitFunctions: [],
  pthreads: {},
  nextWorkerID: 1,
  init() {
    if ((!(ENVIRONMENT_IS_PTHREAD))) {
      PThread.initMainThread();
    }
  },
  initMainThread() {
    var pthreadPoolSize = ((() => {
      try {
        return (n => Number.isInteger(n) && n >= 1 && n <= 128 ? n : 0)(Number((globalThis["ueProfile"] || {})["poolSize"]));
      } catch (e) {
        return 0;
      }
    })() || ((/iPhone|iPad|iPod/.test(navigator.userAgent) || (/Macintosh/.test(navigator.userAgent) && navigator.maxTouchPoints > 1)) ? (/[?&]threads=1(?![^&])/.test(location.search) ? 3 : 1 + ((globalThis["ueProfile"] || {})["audioThread"] ? 1 : 0)) : /Android/.test(navigator.userAgent) ? 24 : Math.max(2, Math.min(navigator.hardwareConcurrency - 4, 12)) + 27));
    // Start loading up the Worker pool, if requested.
    while (pthreadPoolSize--) {
      PThread.allocateUnusedWorker();
    }
    // MINIMAL_RUNTIME takes care of calling loadWasmModuleToAllWorkers
    // in postamble_minimal.js
    addOnPreRun(async () => {
      var pthreadPoolReady = PThread.loadWasmModuleToAllWorkers();
      addRunDependency("loading-workers");
      await pthreadPoolReady;
      removeRunDependency("loading-workers");
    });
  },
  terminateAllThreads: () => {
    assert(!ENVIRONMENT_IS_PTHREAD, "terminateAllThreads() should only be called from the main thread");
    // Attempt to kill all workers.  Sadly (at least on the web) there is no
    // way to terminate a worker synchronously, or to be notified when a
    // worker is actually terminated.  This means there is some risk that
    // pthreads will continue to be executing after `worker.terminate` has
    // returned.  For this reason, we don't call `returnWorkerToPool` here or
    // free the underlying pthread data structures.
    for (var worker of Object.values(PThread.pthreads)) {
      terminateWorker(worker);
    }
    for (var worker of PThread.unusedWorkers) {
      terminateWorker(worker);
    }
    PThread.unusedWorkers = [];
    PThread.pthreads = {};
  },
  clearMailboxAwait: pthread_ptr => {
    if (!waitAsyncPolyfilled) {
      Atomics.notify((growMemViews(), HEAP32), ((pthread_ptr) >> 2));
    }
  },
  terminateRuntime: () => {
    assert(!ENVIRONMENT_IS_PTHREAD, "terminateRuntime() should only be called from the main thread");
    PThread.terminateAllThreads();
    var pthread_ptr = _pthread_self();
    ___set_thread_state(0, 0, 0, 1);
    PThread.clearMailboxAwait(pthread_ptr);
  },
  returnWorkerToPool: worker => {
    // We don't want to run main thread queued calls here, since we are doing
    // some operations that leave the worker queue in an invalid state until
    // we are completely done (it would be bad if free() ends up calling a
    // queued pthread_create which looks at the global data structures we are
    // modifying). To achieve that, defer the free() until the very end, when
    // we are all done.
    var pthread_ptr = worker.pthread_ptr;
    delete PThread.pthreads[pthread_ptr];
    // Note: worker is intentionally not terminated so the pool can
    // dynamically grow.
    PThread.unusedWorkers.push(worker);
    // Not a running Worker anymore
    // Detach the worker from the pthread object, and return it to the
    // worker pool as an unused worker.
    worker.pthread_ptr = 0;
    // Clear any pending waitAsync waiter armed on this thread's struct
    // BEFORE freeing the memory so that memory recycled by malloc in another
    // thread will not have a window where a stale async waiter is still active.
    PThread.clearMailboxAwait(pthread_ptr);
    // Finally, free the underlying (and now-unused) pthread structure in
    // linear memory.
    __emscripten_thread_free_data(pthread_ptr);
  },
  threadInitTLS() {
    // Call thread init functions (these are the _emscripten_tls_init for each
    // module loaded.
    PThread.tlsInitFunctions.forEach(f => f());
  },
  loadWasmModuleToWorker: worker => new Promise(onFinishedLoading => {
    worker.onmessage = e => {
      var d = e.data;
      var cmd = d.cmd;
      // If this message is intended to a recipient that is not the main
      // thread, forward it to the target thread. This is currently only
      // used by `CMD_CHECK_MAILBOX`.
      if (d.targetThread && d.targetThread != _pthread_self()) {
        var targetWorker = PThread.pthreads[d.targetThread];
        if (!targetWorker) err(`worker sent message (${cmd}) to pthread (${d.targetThread}) that no longer exists`);
        targetWorker?.postMessage(d);
        return;
      }
      if (d === "setimmediate" || d === "_si") {
        // Worker wants to postMessage() to itself to implement setImmediate()
        // emulation.
        worker.postMessage(d);
        return;
      }
      switch (cmd) {
       case 4:
        checkMailbox();
        break;

       case 5:
        spawnThread(d);
        break;

       case 6:
        // cleanupThread needs to be run via callUserCallback since it calls
        // back into user code to free thread data. Without this it's possible
        // the unwind or ExitStatus exception could escape here.
        callUserCallback(() => cleanupThread(d.thread));
        break;

       case 3:
        onFinishedLoading(worker);
        break;

       case 9:
        Module[d.handler](...d.args);
        break;

       default:
        // The received message looks like something that should be handled by this message
        // handler, (since there is a e.data.cmd field present), but is not one of the
        // recognized commands:
        if (cmd) err(`worker sent an unknown command ${cmd}`);
      }
    };
    worker.onerror = e => {
      var message = "worker sent an error!";
      if (worker.pthread_ptr) {
        message = `Pthread ${ptrToString(worker.pthread_ptr)} sent an error!`;
      }
      err(`${message} ${e.filename}:${e.lineno}: ${e.message}`);
      throw e;
    };
    assert(wasmMemory instanceof WebAssembly.Memory, "wasmMemory should have been loaded by now");
    assert(wasmModule instanceof WebAssembly.Module, "wasmModule should have been loaded by now");
    // When running on a pthread, none of the incoming parameters on the module
    // object are present. Proxy known handlers back to the main thread if specified.
    var handlers = [];
    var knownHandlers = [ "onExit", "onAbort", "print", "printErr" ];
    for (var handler of knownHandlers) {
      if (Module.propertyIsEnumerable(handler)) {
        handlers.push(handler);
      }
    }
    // Ask the new worker to load up the Emscripten-compiled page. This is a heavy operation.
    worker.postMessage({
      cmd: 1,
      handlers,
      wasmMemory,
      wasmModule,
      workerID: worker.workerID
    });
  }),
  async loadWasmModuleToAllWorkers() {
    // Instantiation is synchronous in pthreads.
    if (ENVIRONMENT_IS_PTHREAD) {
      return;
    }
    let pthreadPoolReady = Promise.all(PThread.unusedWorkers.map(PThread.loadWasmModuleToWorker));
    return pthreadPoolReady;
  },
  allocateUnusedWorker() {
    var worker;
    var pthreadMainJs = _scriptName;
    // We can't use makeModuleReceiveWithVar here since we want to also
    // call URL.createObjectURL on the mainScriptUrlOrBlob.
    if (Module["mainScriptUrlOrBlob"]) {
      pthreadMainJs = Module["mainScriptUrlOrBlob"];
      if (typeof pthreadMainJs != "string") {
        pthreadMainJs = URL.createObjectURL(pthreadMainJs);
      }
    }
    worker = new Worker(pthreadMainJs, {
      // This is the way that we signal to the Web Worker that it is hosting
      // a pthread.
      "name": "em-pthread-" + PThread.nextWorkerID
    });
    worker.workerID = PThread.nextWorkerID++;
    PThread.unusedWorkers.push(worker);
    return worker;
  },
  getNewWorker() {
    if (PThread.unusedWorkers.length == 0) {
      // PTHREAD_POOL_SIZE_STRICT should show a warning and, if set to level `2`, return from the function.
      var newWorker = PThread.allocateUnusedWorker();
      PThread.loadWasmModuleToWorker(newWorker);
    }
    return PThread.unusedWorkers.pop();
  }
};

var onPostRuns = [];

var addOnPostRun = cb => onPostRuns.push(cb);

/** not-@type {!BigUint64Array} */ var HEAPU64;

function establishStackSpace(pthread_ptr) {
  var stackHigh = Number((growMemViews(), HEAPU64)[(((pthread_ptr) + (80)) >> 3)]);
  var stackSize = Number((growMemViews(), HEAPU64)[(((pthread_ptr) + (88)) >> 3)]);
  var stackLow = stackHigh - stackSize;
  assert(stackHigh != 0);
  assert(stackLow != 0);
  assert(stackHigh > stackLow, "stackHigh must be higher then stackLow");
  // Set stack limits used by `emscripten/stack.h` function.  These limits are
  // cached in wasm-side globals to make checks as fast as possible.
  _emscripten_stack_set_limits(stackHigh, stackLow);
  // Call inside wasm module to set up the stack frame for this pthread in wasm module scope
  stackRestore(stackHigh);
  // Write the stack cookie last, after we have set up the proper bounds and
  // current position of the stack.
  writeStackCookie();
}

var wasmTableMirror = [];

var getWasmTableEntry = funcPtr => {
  // Function pointers should show up as numbers, even under wasm64, but
  // we still have some places where bigint values can flow here.
  // https://github.com/emscripten-core/emscripten/issues/18200
  funcPtr = Number(funcPtr);
  var func = wasmTableMirror[funcPtr];
  if (!func) {
    /** @suppress {checkTypes} */ wasmTableMirror[funcPtr] = func = wasmTable.get(funcPtr);
  }
  /** @suppress {checkTypes} */ assert(wasmTable.get(funcPtr) == func, "table mirror is out of date");
  return func;
};

var invokeEntryPoint = (ptr, arg) => {
  // An old thread on this worker may have been canceled without returning the
  // `runtimeKeepaliveCounter` to zero. Reset it now so the new thread won't
  // be affected.
  runtimeKeepaliveCounter = 0;
  // Same for noExitRuntime.  The default for pthreads should always be false
  // otherwise pthreads would never complete and attempts to pthread_join to
  // them would block forever.
  // pthreads can still choose to set `noExitRuntime` explicitly, or
  // call emscripten_unwind_to_js_event_loop to extend their lifetime beyond
  // their main function.  See comment in src/runtime_pthread.js for more.
  noExitRuntime = 0;
  // pthread entry points are always of signature 'void *ThreadMain(void *arg)'
  // Native codebases sometimes spawn threads with other thread entry point
  // signatures, such as void ThreadMain(void *arg), void *ThreadMain(), or
  // void ThreadMain().  That is not acceptable per C/C++ specification, but
  // x86 compiler ABI extensions enable that to work. If you find the
  // following line to crash, either change the signature to "proper" void
  // *ThreadMain(void *arg) form, or try linking with the Emscripten linker
  // flag -sEMULATE_FUNCTION_POINTER_CASTS to add in emulation for this x86
  // ABI extension.
  var result = (a1 => Number(getWasmTableEntry(ptr).call(null, BigInt(a1))))(arg);
  checkStackCookie();
  function finish(result) {
    // In MINIMAL_RUNTIME the noExitRuntime concept does not apply to
    // pthreads. To exit a pthread with live runtime, use the function
    // emscripten_unwind_to_js_event_loop() in the pthread body.
    if (keepRuntimeAlive()) {
      EXITSTATUS = result;
      return;
    }
    __emscripten_thread_exit(result);
  }
  finish(result);
};

var noExitRuntime = false;

var registerTLSInit = tlsInitFunc => PThread.tlsInitFunctions.push(tlsInitFunc);

var runtimeKeepalivePush = () => {
  runtimeKeepaliveCounter += 1;
};

var lengthBytesUTF8 = str => {
  var len = 0;
  for (var i = 0; i < str.length; ++i) {
    // Gotcha: charCodeAt returns a 16-bit word that is a UTF-16 encoded code
    // unit, not a Unicode code point of the character! So decode
    // UTF16->UTF32->UTF8.
    // See http://unicode.org/faq/utf_bom.html#utf16-3
    var c = str.charCodeAt(i);
    // possibly a lead surrogate
    if (c <= 127) {
      len++;
    } else if (c <= 2047) {
      len += 2;
    } else if (c >= 55296 && c <= 57343) {
      len += 4;
      ++i;
    } else {
      len += 3;
    }
  }
  return len;
};

var warnOnce = text => {
  warnOnce.shown ||= {};
  if (!warnOnce.shown[text]) {
    warnOnce.shown[text] = 1;
    err(text);
  }
};

var stringToUTF8Array = (str, heap, outIdx, maxBytesToWrite) => {
  assert(typeof str === "string", `stringToUTF8Array expects a string (got ${typeof str})`);
  // Parameter maxBytesToWrite is not optional. Negative values, 0, null,
  // undefined and false each don't write out any bytes.
  if (!(maxBytesToWrite > 0)) return 0;
  var startIdx = outIdx;
  var endIdx = outIdx + maxBytesToWrite - 1;
  // -1 for string null terminator.
  for (var i = 0; i < str.length; ++i) {
    // For UTF8 byte structure, see http://en.wikipedia.org/wiki/UTF-8#Description
    // and https://www.ietf.org/rfc/rfc2279.txt
    // and https://tools.ietf.org/html/rfc3629
    var u = str.codePointAt(i);
    if (u <= 127) {
      if (outIdx >= endIdx) break;
      heap[outIdx++] = u;
    } else if (u <= 2047) {
      if (outIdx + 1 >= endIdx) break;
      heap[outIdx++] = 192 | (u >> 6);
      heap[outIdx++] = 128 | (u & 63);
    } else if (u <= 65535) {
      if (outIdx + 2 >= endIdx) break;
      heap[outIdx++] = 224 | (u >> 12);
      heap[outIdx++] = 128 | ((u >> 6) & 63);
      heap[outIdx++] = 128 | (u & 63);
    } else {
      if (outIdx + 3 >= endIdx) break;
      if (u > 1114111) warnOnce(`Invalid Unicode code point ${ptrToString(u)} encountered when serializing a JS string to a UTF-8 string in wasm memory! (Valid unicode code points should be in range 0-0x10FFFF).`);
      heap[outIdx++] = 240 | (u >> 18);
      heap[outIdx++] = 128 | ((u >> 12) & 63);
      heap[outIdx++] = 128 | ((u >> 6) & 63);
      heap[outIdx++] = 128 | (u & 63);
      // Gotcha: if codePoint is over 0xFFFF, it is represented as a surrogate pair in UTF-16.
      // We need to manually skip over the second code unit for correct iteration.
      i++;
    }
  }
  // Null-terminate the pointer to the buffer.
  heap[outIdx] = 0;
  return outIdx - startIdx;
};

/** @type {!Uint8Array} */ var HEAPU8;

var stringToUTF8 = (str, outPtr, maxBytesToWrite) => {
  assert(typeof maxBytesToWrite == "number", "stringToUTF8 requires a third parameter that specifies the length of the output buffer");
  return stringToUTF8Array(str, (growMemViews(), HEAPU8), outPtr, maxBytesToWrite);
};

var stringToNewUTF8 = str => {
  var size = lengthBytesUTF8(str) + 1;
  var ret = _malloc(size);
  if (ret) stringToUTF8(str, ret, size);
  return ret;
};

var wasmMemory;

var INT53_MAX = 9007199254740992;

var INT53_MIN = -9007199254740992;

var bigintToI53Checked = num => (num < INT53_MIN || num > INT53_MAX) ? NaN : Number(num);

var UTF8Decoder = globalThis.TextDecoder && new TextDecoder;

/**
   * heapOrArray is either a regular array, or a JavaScript typed array view.
   * @param {number} idx
   * @param {number=} maxBytesToRead
   * @param {boolean=} ignoreNul
   * @return {number}
   */ var findStringEnd = (heapOrArray, idx, maxBytesToRead, ignoreNul) => {
  var maxIdx = idx + maxBytesToRead;
  if (ignoreNul) return maxIdx;
  // TextDecoder needs to know the byte length in advance, it doesn't stop on
  // null terminator by itself.
  // As a tiny code save trick, compare idx against maxIdx using a negation,
  // so that maxBytesToRead=undefined/NaN means Infinity.
  while (heapOrArray[idx] && !(idx >= maxIdx)) ++idx;
  return idx;
};

/**
   * Given a pointer 'idx' to a null-terminated UTF8-encoded string in the given
   * array that contains uint8 values, returns a copy of that string as a
   * Javascript String object.
   * heapOrArray is either a regular array, or a JavaScript typed array view.
   * @param {number=} idx
   * @param {number=} maxBytesToRead
   * @param {boolean=} ignoreNul - If true, the function will not stop on a NUL character.
   * @return {string}
   */ var UTF8ArrayToString = (heapOrArray, idx = 0, maxBytesToRead, ignoreNul) => {
  var endPtr = findStringEnd(heapOrArray, idx, maxBytesToRead, ignoreNul);
  // When using conditional TextDecoder, skip it for short strings as the overhead of the native call is not worth it.
  if (endPtr - idx > 16 && heapOrArray.buffer && UTF8Decoder) {
    return UTF8Decoder.decode(heapOrArray.buffer instanceof ArrayBuffer ? heapOrArray.subarray(idx, endPtr) : heapOrArray.slice(idx, endPtr));
  }
  var str = "";
  while (idx < endPtr) {
    // For UTF8 byte structure, see:
    // http://en.wikipedia.org/wiki/UTF-8#Description
    // https://www.ietf.org/rfc/rfc2279.txt
    // https://tools.ietf.org/html/rfc3629
    var u0 = heapOrArray[idx++];
    if (!(u0 & 128)) {
      str += String.fromCharCode(u0);
      continue;
    }
    var u1 = heapOrArray[idx++] & 63;
    if ((u0 & 224) == 192) {
      str += String.fromCharCode(((u0 & 31) << 6) | u1);
      continue;
    }
    var u2 = heapOrArray[idx++] & 63;
    if ((u0 & 240) == 224) {
      u0 = ((u0 & 15) << 12) | (u1 << 6) | u2;
    } else {
      if ((u0 & 248) != 240) warnOnce(`Invalid UTF-8 leading byte ${ptrToString(u0)} encountered when deserializing a UTF-8 string in wasm memory to a JS string!`);
      u0 = ((u0 & 7) << 18) | (u1 << 12) | (u2 << 6) | (heapOrArray[idx++] & 63);
    }
    if (u0 < 65536) {
      str += String.fromCharCode(u0);
    } else {
      var ch = u0 - 65536;
      str += String.fromCharCode(55296 | (ch >> 10), 56320 | (ch & 1023));
    }
  }
  return str;
};

/**
   * Given a pointer 'ptr' to a null-terminated UTF8-encoded string in the
   * emscripten HEAP, returns a copy of that string as a Javascript String object.
   *
   * @param {number} ptr
   * @param {number=} maxBytesToRead - An optional length that specifies the
   *   maximum number of bytes to read. You can omit this parameter to scan the
   *   string until the first 0 byte. If maxBytesToRead is passed, and the string
   *   at [ptr, ptr+maxBytesToReadr[ contains a null byte in the middle, then the
   *   string will cut short at that byte index.
   * @param {boolean=} ignoreNul - If true, the function will not stop on a NUL character.
   * @return {string}
   */ var UTF8ToString = (ptr, maxBytesToRead, ignoreNul) => {
  assert(typeof ptr == "number", `UTF8ToString expects a number (got ${typeof ptr})`);
  return ptr ? UTF8ArrayToString((growMemViews(), HEAPU8), ptr, maxBytesToRead, ignoreNul) : "";
};

function ___assert_fail(condition, filename, line, func) {
  condition = bigintToI53Checked(condition);
  filename = bigintToI53Checked(filename);
  func = bigintToI53Checked(func);
  return abort(`Assertion failed: ${UTF8ToString(condition)}, at: ` + [ filename ? UTF8ToString(filename) : "unknown filename", line, func ? UTF8ToString(func) : "unknown function" ]);
}

function ___call_sighandler(fp, sig) {
  fp = bigintToI53Checked(fp);
  return getWasmTableEntry(fp)(sig);
}

var exceptionCaught = [];

var uncaughtExceptionCount = 0;

class ExceptionInfo {
  // excPtr - Thrown object pointer to wrap. Metadata pointer is calculated from it.
  constructor(excPtr) {
    this.excPtr = excPtr;
    this.ptr = excPtr - 48;
  }
  set_type(type) {
    (growMemViews(), HEAPU64)[(((this.ptr) + (8)) >> 3)] = BigInt(type);
  }
  get_type() {
    return Number((growMemViews(), HEAPU64)[(((this.ptr) + (8)) >> 3)]);
  }
  set_destructor(destructor) {
    (growMemViews(), HEAPU64)[(((this.ptr) + (16)) >> 3)] = BigInt(destructor);
  }
  get_destructor() {
    return Number((growMemViews(), HEAPU64)[(((this.ptr) + (16)) >> 3)]);
  }
  set_caught(caught) {
    caught = caught ? 1 : 0;
    (growMemViews(), HEAP8)[(this.ptr) + (24)] = caught;
  }
  get_caught() {
    return (growMemViews(), HEAP8)[(this.ptr) + (24)] != 0;
  }
  set_rethrown(rethrown) {
    rethrown = rethrown ? 1 : 0;
    (growMemViews(), HEAP8)[(this.ptr) + (25)] = rethrown;
  }
  get_rethrown() {
    return (growMemViews(), HEAP8)[(this.ptr) + (25)] != 0;
  }
  // Initialize native structure fields. Should be called once after allocated.
  init(type, destructor) {
    this.set_adjusted_ptr(0);
    this.set_type(type);
    this.set_destructor(destructor);
  }
  set_adjusted_ptr(adjustedPtr) {
    (growMemViews(), HEAPU64)[(((this.ptr) + (32)) >> 3)] = BigInt(adjustedPtr);
  }
  get_adjusted_ptr() {
    return Number((growMemViews(), HEAPU64)[(((this.ptr) + (32)) >> 3)]);
  }
}

var ___cxa_begin_catch = function(ptr) {
  ptr = bigintToI53Checked(ptr);
  var ret = (() => {
    var info = new ExceptionInfo(ptr);
    if (!info.get_caught()) {
      info.set_caught(true);
      uncaughtExceptionCount--;
    }
    info.set_rethrown(false);
    exceptionCaught.push(info);
    return ___cxa_get_exception_ptr(ptr);
  })();
  return BigInt(ret);
};

var exceptionLast = null;

var ___cxa_end_catch = () => {
  // Clear state flag.
  _setThrew(0, 0);
  assert(exceptionCaught.length > 0);
  // Call destructor if one is registered then clear it.
  var info = exceptionCaught.pop();
  ___cxa_decrement_exception_refcount(info.excPtr);
  exceptionLast = null;
};

var setTempRet0 = val => __emscripten_tempret_set(val);

var findMatchingCatch = args => {
  var thrown = exceptionLast?.excPtr;
  if (!thrown) {
    // just pass through the null ptr
    setTempRet0(0);
    return 0;
  }
  var info = new ExceptionInfo(thrown);
  info.set_adjusted_ptr(thrown);
  var thrownType = info.get_type();
  if (!thrownType) {
    // just pass through the thrown ptr
    setTempRet0(0);
    return thrown;
  }
  // can_catch receives a **, add indirection
  // The different catch blocks are denoted by different types.
  // Due to inheritance, those types may not precisely match the
  // type of the thrown object. Find one which matches, and
  // return the type of the catch block which should be called.
  for (var caughtType of args) {
    if (!caughtType || caughtType === thrownType) {
      // Catch all clause matched or exactly the same type is caught
      break;
    }
    var adjusted_ptr_addr = info.ptr + 32;
    if (___cxa_can_catch(caughtType, thrownType, adjusted_ptr_addr)) {
      setTempRet0(caughtType);
      return thrown;
    }
  }
  setTempRet0(thrownType);
  return thrown;
};

var ___cxa_find_matching_catch_2 = () => BigInt(findMatchingCatch([]));

var ___cxa_find_matching_catch_3 = arg0 => {
  arg0 = bigintToI53Checked(arg0);
  return BigInt(findMatchingCatch([ arg0 ]));
};

var __Unwind_RaiseException = ex => {
  throw ex;
};

var ___cxa_rethrow = () => {
  if (!exceptionCaught.length) {
    abort("no exception to throw");
  }
  var info = exceptionCaught.at(-1);
  var ptr = info.excPtr;
  info.set_rethrown(true);
  info.set_caught(false);
  uncaughtExceptionCount++;
  ___cxa_increment_exception_refcount(ptr);
  ptr = exceptionLast = new CppException(ptr);
  __Unwind_RaiseException(ptr);
};

var getExceptionMessageCommon = ptr => {
  var sp = stackSave();
  var type_addr_addr = stackAlloc(8);
  var message_addr_addr = stackAlloc(8);
  ___get_exception_message(ptr, type_addr_addr, message_addr_addr);
  var type_addr = Number((growMemViews(), HEAPU64)[((type_addr_addr) >> 3)]);
  var message_addr = Number((growMemViews(), HEAPU64)[((message_addr_addr) >> 3)]);
  var type = UTF8ToString(type_addr);
  _free(type_addr);
  var message;
  if (message_addr) {
    message = UTF8ToString(message_addr);
    _free(message_addr);
  }
  stackRestore(sp);
  return [ type, message ];
};

var getExceptionMessage = exn => getExceptionMessageCommon(exn.excPtr);

var decrementExceptionRefcount = exn => ___cxa_decrement_exception_refcount(exn.excPtr);

var incrementExceptionRefcount = exn => ___cxa_increment_exception_refcount(exn.excPtr);

function ___cxa_throw(ptr, type, destructor) {
  ptr = bigintToI53Checked(ptr);
  type = bigintToI53Checked(type);
  destructor = bigintToI53Checked(destructor);
  var info = new ExceptionInfo(ptr);
  // Initialize ExceptionInfo content after it was allocated in __cxa_allocate_exception.
  info.init(type, destructor);
  ___cxa_increment_exception_refcount(ptr);
  ptr = exceptionLast = new CppException(ptr);
  uncaughtExceptionCount++;
  __Unwind_RaiseException(ptr);
}

var ___cxa_uncaught_exceptions = () => uncaughtExceptionCount;

function pthreadCreateProxied(pthread_ptr, attr, startRoutine, arg) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(2, 0, 1, pthread_ptr, attr, startRoutine, arg);
  return ___pthread_create_js(pthread_ptr, attr, startRoutine, arg);
}

var _emscripten_has_threading_support = () => !!globalThis.SharedArrayBuffer;

function ___pthread_create_js(pthread_ptr, attr, startRoutine, arg) {
  pthread_ptr = bigintToI53Checked(pthread_ptr);
  attr = bigintToI53Checked(attr);
  startRoutine = bigintToI53Checked(startRoutine);
  arg = bigintToI53Checked(arg);
  if (!_emscripten_has_threading_support()) {
    dbg("pthread_create: environment does not support SharedArrayBuffer, pthreads are not available");
    return 6;
  }
  // List of JS objects that will transfer ownership to the Worker hosting the thread
  var transferList = [];
  var error = 0;
  // Synchronously proxy the thread creation to main thread if possible. If we
  // need to transfer ownership of objects, then proxy asynchronously via
  // postMessage.
  if (ENVIRONMENT_IS_PTHREAD && (!transferList.length || error)) {
    return pthreadCreateProxied(pthread_ptr, attr, startRoutine, arg);
  }
  // If on the main thread, and accessing Canvas/OffscreenCanvas failed, abort
  // with the detected error.
  if (error) return error;
  var threadParams = {
    startRoutine,
    pthread_ptr,
    arg,
    transferList
  };
  if (ENVIRONMENT_IS_PTHREAD) {
    // The prepopulated pool of web workers that can host pthreads is stored
    // in the main JS thread. Therefore if a pthread is attempting to spawn a
    // new thread, the thread creation must be deferred to the main JS thread.
    threadParams.cmd = 5;
    postMessage(threadParams, transferList);
    // When we defer thread creation this way, we have no way to detect thread
    // creation synchronously today, so we have to assume success and return 0.
    return 0;
  }
  // We are the main thread, so we have the pthread warmup pool in this
  // thread and can fire off JS thread creation directly ourselves.
  return spawnThread(threadParams);
}

var __Unwind_Resume = ex => {
  throw ex;
};

function ___resumeException(ptr) {
  ptr = bigintToI53Checked(ptr);
  ptr = exceptionLast ??= new CppException(ptr);
  __Unwind_Resume(ptr);
}

var __abort_js = () => abort("native code called abort()");

function __emscripten_init_main_thread_js(tb) {
  tb = bigintToI53Checked(tb);
  var can_block = !ENVIRONMENT_IS_WEB;
  // Feature detect whether the main thread can block.
  try {
    Atomics.wait((growMemViews(), HEAP32), 0, 0, 0);
    can_block = true;
  } catch (e) {}
  // Pass the thread address to the native code where they are stored in wasm
  // globals which act as a form of TLS. Global constructors trying
  // to access this value will read the wrong value, but that is UB anyway.
  __emscripten_thread_init(tb, /*is_main=*/ !ENVIRONMENT_IS_WORKER, /*is_runtime=*/ 1, can_block, /*default_stacksize=*/ 1048576, /*start_profiling=*/ false);
  PThread.threadInitTLS();
}

var handleException = e => {
  // Certain exception types we do not treat as errors since they are used for
  // internal control flow.
  // 1. ExitStatus, which is thrown by exit()
  // 2. "unwind", which is thrown by emscripten_unwind_to_js_event_loop() and others
  //    that wish to return to JS event loop.
  if (e instanceof ExitStatus || e == "unwind") {
    return EXITSTATUS;
  }
  checkStackCookie();
  if (e instanceof WebAssembly.RuntimeError) {
    if (_emscripten_stack_get_current() <= 0) {
      err("Stack overflow detected.  You can try increasing -sSTACK_SIZE (currently set to 4194304)");
    }
  }
  quit_(1, e);
};

var maybeExit = () => {
  if (runtimeExited) {
    return;
  }
  if (!keepRuntimeAlive()) {
    try {
      if (ENVIRONMENT_IS_PTHREAD) {
        // exit the current thread, but only if there is one active.
        // TODO(https://github.com/emscripten-core/emscripten/issues/25076):
        // Unify this check with the runtimeExited check above
        if (_pthread_self()) __emscripten_thread_exit(EXITSTATUS);
        return;
      }
      _exit(EXITSTATUS);
    } catch (e) {
      handleException(e);
    }
  }
};

var callUserCallback = func => {
  if (runtimeExited || ABORT) {
    err("user callback triggered after runtime exited or application aborted.  Ignoring.");
    return;
  }
  try {
    return func();
  } catch (e) {
    handleException(e);
  } finally {
    maybeExit();
  }
};

function __emscripten_thread_mailbox_await(pthread_ptr) {
  pthread_ptr = bigintToI53Checked(pthread_ptr);
  if (!waitAsyncPolyfilled) {
    // Wait on the pthread's initial self-pointer field because it is easy and
    // safe to access from sending threads that need to notify the waiting
    // thread.
    // Note: Under wasm64 only the low 32-bit of the pthread_ptr are
    // read/compared here, but we don't actually care about the exact values
    // here as long as they match.
    var wait = Atomics.waitAsync((growMemViews(), HEAP32), ((pthread_ptr) >> 2), pthread_ptr);
    assert(wait.async);
    wait.value.then(checkMailbox);
    var waitingAsync = pthread_ptr + 204;
    Atomics.store((growMemViews(), HEAP32), ((waitingAsync) >> 2), 1);
  }
}

var checkMailbox = () => {
  // checkMailbox can be called after the pthread has shut down. See
  // Pthread.terminateRuntime().
  // In this case we return silently without re-registering using waitAsync.
  // Perhaps there is a more universal way we can detect runtime has exited.
  // TODO(https://github.com/emscripten-core/emscripten/issues/25076)
  var pthread_ptr = _pthread_self();
  if (!pthread_ptr) return;
  callUserCallback(() => {
    // If we are using Atomics.waitAsync as our notification mechanism, wait
    // for a notification before processing the mailbox to avoid missing any
    // work that could otherwise arrive after we've finished processing the
    // mailbox and before we're ready for the next notification.
    __emscripten_thread_mailbox_await(pthread_ptr);
    __emscripten_check_mailbox();
  });
};

function __emscripten_notify_mailbox_postmessage(targetThread, currThreadId) {
  targetThread = bigintToI53Checked(targetThread);
  currThreadId = bigintToI53Checked(currThreadId);
  if (targetThread == currThreadId) {
    setTimeout(checkMailbox);
  } else if (ENVIRONMENT_IS_PTHREAD) {
    postMessage({
      targetThread,
      cmd: 4
    });
  } else {
    var worker = PThread.pthreads[targetThread];
    if (!worker) {
      err(`Cannot send message to thread with ID ${targetThread}, unknown thread ID!`);
      return;
    }
    worker.postMessage({
      cmd: 4
    });
  }
}

var proxiedJSCallArgs = [];

function __emscripten_receive_on_main_thread_js(funcIndex, emAsmAddr, callingThread, bufSize, args, ctx, ctxArgs) {
  emAsmAddr = bigintToI53Checked(emAsmAddr);
  callingThread = bigintToI53Checked(callingThread);
  args = bigintToI53Checked(args);
  ctx = bigintToI53Checked(ctx);
  ctxArgs = bigintToI53Checked(ctxArgs);
  // Sometimes we need to backproxy events to the calling thread (e.g.
  // HTML5 DOM events handlers such as
  // emscripten_set_mousemove_callback()), so keep track in a globally
  // accessible variable about the thread that initiated the proxying.
  proxiedJSCallArgs.length = 0;
  var b = ((args) >> 3);
  var end = ((args + bufSize) >> 3);
  while (b < end) {
    var arg;
    if ((growMemViews(), HEAP64)[b++]) {
      // It's a BigInt.
      arg = (growMemViews(), HEAP64)[b++];
    } else {
      // It's a Number.
      arg = (growMemViews(), HEAPF64)[b++];
    }
    proxiedJSCallArgs.push(arg);
  }
  // Proxied JS library funcs use funcIndex and EM_ASM functions use emAsmAddr
  var func = emAsmAddr ? ASM_CONSTS[emAsmAddr] : proxiedFunctionTable[funcIndex];
  assert(!(funcIndex && emAsmAddr));
  assert(func.length == proxiedJSCallArgs.length, "Call args mismatch in _emscripten_receive_on_main_thread_js");
  PThread.currentProxiedOperationCallerThread = callingThread;
  var rtn = func(...proxiedJSCallArgs);
  PThread.currentProxiedOperationCallerThread = 0;
  if (ctx) {
    // A PROXY_SYNC_ASYNC function may complete synchronously with a plain value.
    Promise.resolve(rtn).then(rtn => __emscripten_run_js_on_main_thread_done(ctx, ctxArgs, rtn));
    return;
  }
  // In memory64 mode some proxied functions return bigint/pointer but
  // our return type is i53/double.
  if (typeof rtn == "bigint") {
    rtn = bigintToI53Checked(rtn);
  }
  // Proxied functions can return any type except bigint.  All other types
  // coerce to f64/double (the return type of this function in C) but not
  // bigint.
  assert(typeof rtn != "bigint");
  return rtn;
}

var __emscripten_runtime_keepalive_clear = () => {
  noExitRuntime = false;
  runtimeKeepaliveCounter = 0;
};

function __emscripten_thread_cleanup(thread) {
  thread = bigintToI53Checked(thread);
  // Called when a thread needs to be cleaned up so it can be reused.
  // A thread is considered reusable when it either returns from its
  // entry point, calls pthread_exit, or acts upon a cancellation.
  // Detached threads are responsible for calling this themselves,
  // otherwise pthread_join is responsible for calling this.
  if (!ENVIRONMENT_IS_PTHREAD) cleanupThread(thread); else postMessage({
    cmd: 6,
    thread
  });
}

function __emscripten_thread_set_strongref(thread) {
  thread = bigintToI53Checked(thread);
}

var __emscripten_throw_longjmp = () => {
  throw new EmscriptenSjLj;
};

function __gmtime_js(time, tmPtr) {
  time = bigintToI53Checked(time);
  tmPtr = bigintToI53Checked(tmPtr);
  var date = new Date(time * 1e3);
  if (isNaN(date.getTime())) {
    return 1;
  }
  (growMemViews(), HEAP32)[((tmPtr) >> 2)] = date.getUTCSeconds();
  (growMemViews(), HEAP32)[(((tmPtr) + (4)) >> 2)] = date.getUTCMinutes();
  (growMemViews(), HEAP32)[(((tmPtr) + (8)) >> 2)] = date.getUTCHours();
  (growMemViews(), HEAP32)[(((tmPtr) + (12)) >> 2)] = date.getUTCDate();
  (growMemViews(), HEAP32)[(((tmPtr) + (16)) >> 2)] = date.getUTCMonth();
  (growMemViews(), HEAP32)[(((tmPtr) + (20)) >> 2)] = date.getUTCFullYear() - 1900;
  (growMemViews(), HEAP32)[(((tmPtr) + (24)) >> 2)] = date.getUTCDay();
  var start = Date.UTC(date.getUTCFullYear(), 0, 1, 0, 0, 0, 0);
  var yday = ((date.getTime() - start) / (1e3 * 60 * 60 * 24)) | 0;
  (growMemViews(), HEAP32)[(((tmPtr) + (28)) >> 2)] = yday;
  return 0;
}

var isLeapYear = year => year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0);

var MONTH_DAYS_LEAP_CUMULATIVE = [ 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335 ];

var MONTH_DAYS_REGULAR_CUMULATIVE = [ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 ];

var ydayFromDate = date => {
  var leap = isLeapYear(date.getFullYear());
  var monthDaysCumulative = (leap ? MONTH_DAYS_LEAP_CUMULATIVE : MONTH_DAYS_REGULAR_CUMULATIVE);
  var yday = monthDaysCumulative[date.getMonth()] + date.getDate() - 1;
  // -1 since it's days since Jan 1
  return yday;
};

function __localtime_js(time, tmPtr) {
  time = bigintToI53Checked(time);
  tmPtr = bigintToI53Checked(tmPtr);
  var date = new Date(time * 1e3);
  if (isNaN(date.getTime())) {
    return 1;
  }
  (growMemViews(), HEAP32)[((tmPtr) >> 2)] = date.getSeconds();
  (growMemViews(), HEAP32)[(((tmPtr) + (4)) >> 2)] = date.getMinutes();
  (growMemViews(), HEAP32)[(((tmPtr) + (8)) >> 2)] = date.getHours();
  (growMemViews(), HEAP32)[(((tmPtr) + (12)) >> 2)] = date.getDate();
  (growMemViews(), HEAP32)[(((tmPtr) + (16)) >> 2)] = date.getMonth();
  (growMemViews(), HEAP32)[(((tmPtr) + (20)) >> 2)] = date.getFullYear() - 1900;
  (growMemViews(), HEAP32)[(((tmPtr) + (24)) >> 2)] = date.getDay();
  var yday = ydayFromDate(date) | 0;
  (growMemViews(), HEAP32)[(((tmPtr) + (28)) >> 2)] = yday;
  (growMemViews(), HEAP64)[(((tmPtr) + (40)) >> 3)] = BigInt(-(date.getTimezoneOffset() * 60));
  // Attention: DST is in December in South, and some regions don't have DST at all.
  var start = new Date(date.getFullYear(), 0, 1);
  var summerOffset = new Date(date.getFullYear(), 6, 1).getTimezoneOffset();
  var winterOffset = start.getTimezoneOffset();
  var dst = (summerOffset != winterOffset && date.getTimezoneOffset() == Math.min(winterOffset, summerOffset)) | 0;
  (growMemViews(), HEAP32)[(((tmPtr) + (32)) >> 2)] = dst;
  return 0;
}

var __tzset_js = function(timezone, daylight, std_name, dst_name) {
  timezone = bigintToI53Checked(timezone);
  daylight = bigintToI53Checked(daylight);
  std_name = bigintToI53Checked(std_name);
  dst_name = bigintToI53Checked(dst_name);
  // TODO: Use (malleable) environment variables instead of system settings.
  var currentYear = (new Date).getFullYear();
  var winter = new Date(currentYear, 0, 1);
  var summer = new Date(currentYear, 6, 1);
  var winterOffset = winter.getTimezoneOffset();
  var summerOffset = summer.getTimezoneOffset();
  // Local standard timezone offset. Local standard time is not adjusted for
  // daylight savings.  This code uses the fact that getTimezoneOffset returns
  // a greater value during Standard Time versus Daylight Saving Time (DST).
  // Thus it determines the expected output during Standard Time, and it
  // compares whether the output of the given date the same (Standard) or less
  // (DST).
  var stdTimezoneOffset = Math.max(winterOffset, summerOffset);
  // timezone is specified as seconds west of UTC ("The external variable
  // `timezone` shall be set to the difference, in seconds, between
  // Coordinated Universal Time (UTC) and local standard time."), the same
  // as returned by stdTimezoneOffset.
  // See http://pubs.opengroup.org/onlinepubs/009695399/functions/tzset.html
  (growMemViews(), HEAPU64)[((timezone) >> 3)] = BigInt(stdTimezoneOffset * 60);
  (growMemViews(), HEAP32)[((daylight) >> 2)] = Number(winterOffset != summerOffset);
  var extractZone = timezoneOffset => {
    // Why inverse sign?
    // Read here https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Date/getTimezoneOffset
    var sign = timezoneOffset >= 0 ? "-" : "+";
    var absOffset = Math.abs(timezoneOffset);
    var hours = String(Math.floor(absOffset / 60)).padStart(2, "0");
    var minutes = String(absOffset % 60).padStart(2, "0");
    return `UTC${sign}${hours}${minutes}`;
  };
  var winterName = extractZone(winterOffset);
  var summerName = extractZone(summerOffset);
  assert(winterName);
  assert(summerName);
  assert(lengthBytesUTF8(winterName) <= 16, `timezone name truncated to fit in TZNAME_MAX (${winterName})`);
  assert(lengthBytesUTF8(summerName) <= 16, `timezone name truncated to fit in TZNAME_MAX (${summerName})`);
  if (summerOffset < winterOffset) {
    // Northern hemisphere
    stringToUTF8(winterName, std_name, 17);
    stringToUTF8(summerName, dst_name, 17);
  } else {
    stringToUTF8(winterName, dst_name, 17);
    stringToUTF8(summerName, std_name, 17);
  }
};

function __wasmfs_copy_preloaded_file_data(index, buffer) {
  buffer = bigintToI53Checked(buffer);
  return (growMemViews(), HEAPU8).set(wasmFSPreloadedFiles[index].fileData, buffer);
}

var wasmFS$backends = {};

var wasmFS$JSMemoryRanges = {};

async function __wasmfs_create_fetch_backend_js(backend) {
  backend = bigintToI53Checked(backend);
  function fileUrlOf(file) {
    var fileUrl = UTF8ToString(__wasmfs_fetch_get_file_url(file));
    if (fileUrl.indexOf("://") !== -1) {
      return fileUrl;
    }
    try {
      return new URL(fileUrl, self.location.origin).toString();
    } catch (_e) {
      throw {
        status: 404
      };
    }
  }
  // The first access of each file, shared by every call that arrives while it is in flight: a second GET would store a
  // second info object over the first, orphaning the chunks that the first call goes on to fetch into it.
  // Resolves to true when the file is complete (nothing more to read for this call)
  var firstAccesses = {};
  async function firstAccess(file, url) {
    // First access: one ranged GET of the first two chunks; of an IoStore container only its first byte, for its size
    // (change 4)
    var chunkSize = __wasmfs_fetch_get_chunk_size(file);
    var sizeOnly = /\.ucas(?:$|[?#])/i.test(url);
    var first = await fetch(url, {
      headers: {
        "Range": `bytes=0-${sizeOnly ? 0 : chunkSize * 2 - 1}`
      }
    });
    if (first.status == 416) {
      wasmFS$JSMemoryRanges[file] = {
        size: 0,
        chunks: [ new Uint8Array(0) ],
        chunkSize: 1
      };
      return true;
    }
    if (!first.ok) {
      throw first;
    }
    var data = new Uint8Array(await first.arrayBuffer());
    var range = first.status == 206 ? /^bytes 0-\d+\/(\d+)$/.exec(first.headers.get("Content-Range") || "") : null;
    if (first.status == 206 && !range) {
      console.error(`WasmFS fetch: ${url} asked bytes 0-${chunkSize * 2 - 1}, got ${first.headers.get("Content-Range")}`);
      throw {
        status: 0
      };
    }
    var total = range ? parseInt(range[1], 10) : data.byteLength;
    if (total <= data.byteLength) {
      // The whole file: one chunk
      wasmFS$JSMemoryRanges[file] = {
        size: data.byteLength,
        chunks: [ data ],
        chunkSize: Math.max(1, data.byteLength)
      };
      return true;
    }
    if (sizeOnly) {
      // The size only: the caller fetches the chunks it covers, if any
      wasmFS$JSMemoryRanges[file] = {
        size: total,
        chunks: [],
        chunkSize
      };
      return;
    }
    // A short 206 (fewer bytes than asked and than the file has) keeps only the whole chunks it
    // carries; a partial chunk marked present would be read as the file's bytes (silent corruption)
    var expected = Math.min(chunkSize * 2, total);
    if (data.byteLength < expected && data.byteLength < chunkSize) {
      console.error(`WasmFS fetch: ${url} asked bytes 0-${expected - 1}, got ${data.byteLength} bytes`);
      throw {
        status: 0
      };
    }
    var chunks = [ data.slice(0, chunkSize) ];
    if (data.byteLength >= expected) {
      chunks.push(data.slice(chunkSize, expected));
    }
    wasmFS$JSMemoryRanges[file] = {
      size: total,
      chunks,
      chunkSize
    };
  }
  // Makes sure the chunks covering [offset, offset+len) are in JS memory
  async function getFileRange(file, offset, len) {
    var url = fileUrlOf(file);
    offset ??= 0;
    var info = wasmFS$JSMemoryRanges[file];
    if (!info) {
      var pending = firstAccesses[file] ??= firstAccess(file, url).finally(() => {
        delete firstAccesses[file];
      });
      if (await pending) {
        return;
      }
      info = wasmFS$JSMemoryRanges[file];
    }
    var size = info.size;
    var chunkSize = info.chunkSize;
    len ??= chunkSize;
    len = Math.min(len, size - offset);
    if (len <= 0) {
      return;
    }
    // Math.floor, not | 0: offsets of 2 GiB and more overflow a 32-bit integer
    var firstChunk = Math.floor(offset / chunkSize);
    var lastChunk = Math.floor((offset + len - 1) / chunkSize);
    var allPresent = true;
    for (var i = firstChunk; i <= lastChunk; i++) {
      if (!info.chunks[i]) {
        allPresent = false;
        break;
      }
    }
    if (allPresent) {
      return;
    }
    // One request for every chunk the read needs, up to the last byte of the last chunk
    var start = firstChunk * chunkSize;
    var end = Math.min((lastChunk + 1) * chunkSize, size);
    var response = await fetch(url, {
      headers: {
        "Range": `bytes=${start}-${end - 1}`
      }
    });
    if (!response.ok) {
      throw response;
    }
    var bytes = new Uint8Array(await response.arrayBuffer());
    // A measured run read a corrupt .ucas: only a 206 whose Content-Range starts at `start` and covers the range is
    // sliced as asked; a 200 of the whole file (a host that ignores Range) is cut to the range; anything else is an error
    var got = response.status == 206 ? /^bytes (\d+)-(\d+)\//.exec(response.headers.get("Content-Range") || "") : null;
    if (got) {
      if (parseInt(got[1], 10) != start || bytes.byteLength < end - start) {
        console.error(`WasmFS fetch: ${url} asked bytes ${start}-${end - 1}, got ${response.headers.get("Content-Range")} (${bytes.byteLength} bytes)`);
        throw {
          status: 0
        };
      }
    } else if (response.status == 200 && bytes.byteLength == size) {
      bytes = bytes.subarray(start, end);
    } else {
      console.error(`WasmFS fetch: ${url} asked bytes ${start}-${end - 1}, got status ${response.status} with ${bytes.byteLength} bytes`);
      throw {
        status: 0
      };
    }
    for (i = firstChunk; i <= lastChunk; i++) {
      info.chunks[i] = bytes.slice(i * chunkSize - start, (i + 1) * chunkSize - start);
    }
  }
  wasmFS$backends[backend] = {
    allocFile: async file => {},
    freeFile: async file => {
      delete wasmFS$JSMemoryRanges[file];
    },
    // UE 5.9 change (5): the backend is read-only. A write fails with EROFS, so the caller's write fails like any other failed
    // write; the original returned undefined, which WasmFS cannot convert to its result and which ended the calling thread.
    write: async (file, buffer, length, offset) => -69,
    read: async (file, buffer, length, offset) => {
      if (offset < 0 || length <= 0) {
        return 0;
      }
      // Change 6: a failure other than a 404 is tried again twice before the read fails
      for (var attempt = 0; ;attempt++) {
        try {
          await getFileRange(file, offset || 0, length);
          break;
        } catch (failedResponse) {
          if (failedResponse.status === 404) {
            return -44;
          }
          var why = failedResponse && failedResponse.status !== undefined ? `status ${failedResponse.status}` : String(failedResponse);
          if (attempt >= 2) {
            console.warn(`WasmFS fetch: read of ${UTF8ToString(__wasmfs_fetch_get_file_url(file))} at ${offset}+${length} failed (${why}) after ${attempt + 1} tries`);
            return -8;
          }
          await new Promise(resolve => setTimeout(resolve, 250 * (attempt + 1)));
        }
      }
      var info = wasmFS$JSMemoryRanges[file];
      length = Math.min(length, info.size - offset);
      if (length <= 0) {
        return 0;
      }
      var chunkSize = info.chunkSize;
      var firstChunk = Math.floor(offset / chunkSize);
      var lastChunk = Math.floor((offset + length - 1) / chunkSize);
      var readLength = 0;
      for (var i = firstChunk; i <= lastChunk; i++) {
        var chunk = info.chunks[i];
        var chunkStart = i * chunkSize;
        var start = Math.max(chunkStart, offset);
        var end = Math.min(chunkStart + chunkSize, offset + length);
        (growMemViews(), HEAPU8).set(chunk.subarray(start - chunkStart, end - chunkStart), buffer + (start - offset));
        readLength = end - offset;
      }
      return readLength;
    },
    getSize: async file => {
      try {
        await getFileRange(file, 0, 0);
      } catch (failedResponse) {
        return 0;
      }
      return wasmFS$JSMemoryRanges[file].size;
    }
  };
}

var wasmFSPreloadedDirs = [];

var __wasmfs_get_num_preloaded_dirs = () => wasmFSPreloadedDirs.length;

var wasmFSPreloadedFiles = [];

var wasmFSPreloadingFlushed = false;

var __wasmfs_get_num_preloaded_files = () => {
  // When this method is called from WasmFS it means that we are about to
  // flush all the preloaded data, so mark that. (There is no call that
  // occurs at the end of that flushing, which would be more natural, but it
  // is fine to mark the flushing here as during the flushing itself no user
  // code can run, so nothing will check whether we have flushed or not.)
  wasmFSPreloadingFlushed = true;
  return wasmFSPreloadedFiles.length;
};

function __wasmfs_get_preloaded_child_path(index, childNameBuffer) {
  childNameBuffer = bigintToI53Checked(childNameBuffer);
  var s = wasmFSPreloadedDirs[index].childName;
  var len = lengthBytesUTF8(s) + 1;
  stringToUTF8(s, childNameBuffer, len);
}

var __wasmfs_get_preloaded_file_mode = index => wasmFSPreloadedFiles[index].mode;

var __wasmfs_get_preloaded_file_size = index => BigInt(wasmFSPreloadedFiles[index].fileData.length);

function __wasmfs_get_preloaded_parent_path(index, parentPathBuffer) {
  parentPathBuffer = bigintToI53Checked(parentPathBuffer);
  var s = wasmFSPreloadedDirs[index].parentPath;
  var len = lengthBytesUTF8(s) + 1;
  stringToUTF8(s, parentPathBuffer, len);
}

function __wasmfs_get_preloaded_path_name(index, fileNameBuffer) {
  fileNameBuffer = bigintToI53Checked(fileNameBuffer);
  var s = wasmFSPreloadedFiles[index].pathName;
  var len = lengthBytesUTF8(s) + 1;
  stringToUTF8(s, fileNameBuffer, len);
}

function __wasmfs_jsimpl_alloc_file(backend, file) {
  backend = bigintToI53Checked(backend);
  file = bigintToI53Checked(file);
  assert(wasmFS$backends[backend]);
  return wasmFS$backends[backend].allocFile(file);
}

async function __wasmfs_jsimpl_async_alloc_file(ctx, backend, file) {
  ctx = bigintToI53Checked(ctx);
  backend = bigintToI53Checked(backend);
  file = bigintToI53Checked(file);
  assert(wasmFS$backends[backend]);
  await wasmFS$backends[backend].allocFile(file);
  _emscripten_proxy_finish(ctx);
}

async function __wasmfs_jsimpl_async_free_file(ctx, backend, file) {
  ctx = bigintToI53Checked(ctx);
  backend = bigintToI53Checked(backend);
  file = bigintToI53Checked(file);
  assert(wasmFS$backends[backend]);
  await wasmFS$backends[backend].freeFile(file);
  _emscripten_proxy_finish(ctx);
}

async function __wasmfs_jsimpl_async_get_size(ctx, backend, file, size_p) {
  ctx = bigintToI53Checked(ctx);
  backend = bigintToI53Checked(backend);
  file = bigintToI53Checked(file);
  size_p = bigintToI53Checked(size_p);
  assert(wasmFS$backends[backend]);
  var size = await wasmFS$backends[backend].getSize(file);
  (growMemViews(), HEAP64)[((size_p) >> 3)] = BigInt(size);
  _emscripten_proxy_finish(ctx);
}

async function __wasmfs_jsimpl_async_read(ctx, backend, file, buffer, length, offset, result_p) {
  ctx = bigintToI53Checked(ctx);
  backend = bigintToI53Checked(backend);
  file = bigintToI53Checked(file);
  buffer = bigintToI53Checked(buffer);
  length = bigintToI53Checked(length);
  offset = bigintToI53Checked(offset);
  result_p = bigintToI53Checked(result_p);
  assert(wasmFS$backends[backend]);
  var result = await wasmFS$backends[backend].read(file, buffer, length, offset);
  (growMemViews(), HEAPU64)[((result_p) >> 3)] = BigInt(result);
  _emscripten_proxy_finish(ctx);
}

async function __wasmfs_jsimpl_async_write(ctx, backend, file, buffer, length, offset, result_p) {
  ctx = bigintToI53Checked(ctx);
  backend = bigintToI53Checked(backend);
  file = bigintToI53Checked(file);
  buffer = bigintToI53Checked(buffer);
  length = bigintToI53Checked(length);
  offset = bigintToI53Checked(offset);
  result_p = bigintToI53Checked(result_p);
  assert(wasmFS$backends[backend]);
  var result = await wasmFS$backends[backend].write(file, buffer, length, offset);
  (growMemViews(), HEAPU64)[((result_p) >> 3)] = BigInt(result);
  _emscripten_proxy_finish(ctx);
}

function __wasmfs_jsimpl_free_file(backend, file) {
  backend = bigintToI53Checked(backend);
  file = bigintToI53Checked(file);
  assert(wasmFS$backends[backend]);
  return wasmFS$backends[backend].freeFile(file);
}

function __wasmfs_jsimpl_get_size(backend, file) {
  backend = bigintToI53Checked(backend);
  file = bigintToI53Checked(file);
  assert(wasmFS$backends[backend]);
  return wasmFS$backends[backend].getSize(file);
}

function __wasmfs_jsimpl_read(backend, file, buffer, length, offset) {
  backend = bigintToI53Checked(backend);
  file = bigintToI53Checked(file);
  buffer = bigintToI53Checked(buffer);
  length = bigintToI53Checked(length);
  offset = bigintToI53Checked(offset);
  assert(wasmFS$backends[backend]);
  if (!wasmFS$backends[backend].read) {
    return -28;
  }
  return wasmFS$backends[backend].read(file, buffer, length, offset);
}

function __wasmfs_jsimpl_set_size(backend, file, size) {
  backend = bigintToI53Checked(backend);
  file = bigintToI53Checked(file);
  size = bigintToI53Checked(size);
  assert(wasmFS$backends[backend]);
  return wasmFS$backends[backend].setSize(file, size);
}

function __wasmfs_jsimpl_write(backend, file, buffer, length, offset) {
  backend = bigintToI53Checked(backend);
  file = bigintToI53Checked(file);
  buffer = bigintToI53Checked(buffer);
  length = bigintToI53Checked(length);
  offset = bigintToI53Checked(offset);
  assert(wasmFS$backends[backend]);
  if (!wasmFS$backends[backend].write) {
    return -28;
  }
  return wasmFS$backends[backend].write(file, buffer, length, offset);
}

class HandleAllocator {
  allocated=[ undefined ];
  freelist=[];
  get(id) {
    assert(this.allocated[id] !== undefined, `invalid handle: ${id}`);
    return this.allocated[id];
  }
  has(id) {
    return this.allocated[id] !== undefined;
  }
  allocate(handle) {
    var id = this.freelist.pop() ?? this.allocated.length;
    this.allocated[id] = handle;
    return id;
  }
  free(id) {
    assert(this.allocated[id] !== undefined);
    // Set the slot to `undefined` rather than using `delete` here since
    // apparently arrays with holes in them can be less efficient.
    this.allocated[id] = undefined;
    this.freelist.push(id);
  }
}

var wasmfsOPFSAccessHandles = new HandleAllocator;

var wasmfsOPFSProxyFinish = ctx => {
  // When using pthreads the proxy needs to know when the work is finished.
  // When used with JSPI the work will be executed in an async block so there
  // is no need to notify when done.
  _emscripten_proxy_finish(ctx);
};

async function __wasmfs_opfs_close_access(ctx, accessID, errPtr) {
  ctx = bigintToI53Checked(ctx);
  errPtr = bigintToI53Checked(errPtr);
  let accessHandle = wasmfsOPFSAccessHandles.get(accessID);
  try {
    await accessHandle.close();
  } catch {
    let err = -29;
    (growMemViews(), HEAP32)[((errPtr) >> 2)] = err;
  }
  wasmfsOPFSAccessHandles.free(accessID);
  wasmfsOPFSProxyFinish(ctx);
}

var wasmfsOPFSBlobs = new HandleAllocator;

var __wasmfs_opfs_close_blob = blobID => {
  wasmfsOPFSBlobs.free(blobID);
};

async function __wasmfs_opfs_flush_access(ctx, accessID, errPtr) {
  ctx = bigintToI53Checked(ctx);
  errPtr = bigintToI53Checked(errPtr);
  let accessHandle = wasmfsOPFSAccessHandles.get(accessID);
  try {
    await accessHandle.flush();
  } catch {
    let err = -29;
    (growMemViews(), HEAP32)[((errPtr) >> 2)] = err;
  }
  wasmfsOPFSProxyFinish(ctx);
}

var wasmfsOPFSDirectoryHandles = new HandleAllocator;

var __wasmfs_opfs_free_directory = dirID => {
  wasmfsOPFSDirectoryHandles.free(dirID);
};

var wasmfsOPFSFileHandles = new HandleAllocator;

var __wasmfs_opfs_free_file = fileID => {
  wasmfsOPFSFileHandles.free(fileID);
};

var wasmfsOPFSGetOrCreateFile = async (parent, name, create) => {
  let parentHandle = wasmfsOPFSDirectoryHandles.get(parent);
  let fileHandle;
  try {
    fileHandle = await parentHandle.getFileHandle(name, {
      create
    });
  } catch (e) {
    if (e.name === "NotFoundError") {
      return -20;
    }
    if (e.name === "TypeMismatchError") {
      return -31;
    }
    err("unexpected error:", e, e.stack);
    return -29;
  }
  return wasmfsOPFSFileHandles.allocate(fileHandle);
};

var wasmfsOPFSGetOrCreateDir = async (parent, name, create) => {
  let parentHandle = wasmfsOPFSDirectoryHandles.get(parent);
  let childHandle;
  try {
    childHandle = await parentHandle.getDirectoryHandle(name, {
      create
    });
  } catch (e) {
    if (e.name === "NotFoundError") {
      return -20;
    }
    if (e.name === "TypeMismatchError") {
      return -54;
    }
    err("unexpected error:", e, e.stack);
    return -29;
  }
  return wasmfsOPFSDirectoryHandles.allocate(childHandle);
};

async function __wasmfs_opfs_get_child(ctx, parent, namePtr, childTypePtr, childIDPtr) {
  ctx = bigintToI53Checked(ctx);
  namePtr = bigintToI53Checked(namePtr);
  childTypePtr = bigintToI53Checked(childTypePtr);
  childIDPtr = bigintToI53Checked(childIDPtr);
  let name = UTF8ToString(namePtr);
  let childType = 1;
  let childID = await wasmfsOPFSGetOrCreateFile(parent, name, false);
  if (childID == -31) {
    childType = 2;
    childID = await wasmfsOPFSGetOrCreateDir(parent, name, false);
  }
  (growMemViews(), HEAP32)[((childTypePtr) >> 2)] = childType;
  (growMemViews(), HEAP32)[((childIDPtr) >> 2)] = childID;
  wasmfsOPFSProxyFinish(ctx);
}

async function __wasmfs_opfs_get_entries(ctx, dirID, entriesPtr, errPtr) {
  ctx = bigintToI53Checked(ctx);
  entriesPtr = bigintToI53Checked(entriesPtr);
  errPtr = bigintToI53Checked(errPtr);
  let dirHandle = wasmfsOPFSDirectoryHandles.get(dirID);
  // TODO: Use 'for await' once Acorn supports that.
  try {
    let iter = dirHandle.entries();
    for (let entry; entry = await iter.next(), !entry.done; ) {
      let [name, child] = entry.value;
      let sp = stackSave();
      let namePtr = stringToUTF8OnStack(name);
      let type = child.kind == "file" ? 1 : 2;
      __wasmfs_opfs_record_entry(entriesPtr, namePtr, type);
      stackRestore(sp);
    }
  } catch {
    let err = -29;
    (growMemViews(), HEAP32)[((errPtr) >> 2)] = err;
  }
  wasmfsOPFSProxyFinish(ctx);
}

async function __wasmfs_opfs_get_size_access(ctx, accessID, sizePtr) {
  ctx = bigintToI53Checked(ctx);
  sizePtr = bigintToI53Checked(sizePtr);
  let accessHandle = wasmfsOPFSAccessHandles.get(accessID);
  let size;
  try {
    size = await accessHandle.getSize();
  } catch {
    size = -29;
  }
  (growMemViews(), HEAP64)[((sizePtr) >> 3)] = BigInt(size);
  wasmfsOPFSProxyFinish(ctx);
}

var __wasmfs_opfs_get_size_blob = function(blobID) {
  var ret = (() => wasmfsOPFSBlobs.get(blobID).size)();
  return BigInt(ret);
};

async function __wasmfs_opfs_get_size_file(ctx, fileID, sizePtr) {
  ctx = bigintToI53Checked(ctx);
  sizePtr = bigintToI53Checked(sizePtr);
  let fileHandle = wasmfsOPFSFileHandles.get(fileID);
  let size;
  try {
    size = (await fileHandle.getFile()).size;
  } catch {
    size = -29;
  }
  (growMemViews(), HEAP64)[((sizePtr) >> 3)] = BigInt(size);
  wasmfsOPFSProxyFinish(ctx);
}

async function __wasmfs_opfs_init_root_directory(ctx) {
  ctx = bigintToI53Checked(ctx);
  // allocated.length starts off as 1 since 0 is a reserved handle
  if (wasmfsOPFSDirectoryHandles.allocated.length == 1) {
    // Closure compiler errors on this as it does not recognize the OPFS
    // API yet, it seems. Unfortunately an existing annotation for this is in
    // the closure compiler codebase, and cannot be overridden in user code
    // (it complains on a duplicate type annotation), so just suppress it.
    /** @suppress {checkTypes} */ let root = await navigator.storage.getDirectory();
    wasmfsOPFSDirectoryHandles.allocated.push(root);
  }
  wasmfsOPFSProxyFinish(ctx);
}

async function __wasmfs_opfs_insert_directory(ctx, parent, namePtr, childIDPtr) {
  ctx = bigintToI53Checked(ctx);
  namePtr = bigintToI53Checked(namePtr);
  childIDPtr = bigintToI53Checked(childIDPtr);
  let name = UTF8ToString(namePtr);
  let childID = await wasmfsOPFSGetOrCreateDir(parent, name, true);
  (growMemViews(), HEAP32)[((childIDPtr) >> 2)] = childID;
  wasmfsOPFSProxyFinish(ctx);
}

async function __wasmfs_opfs_insert_file(ctx, parent, namePtr, childIDPtr) {
  ctx = bigintToI53Checked(ctx);
  namePtr = bigintToI53Checked(namePtr);
  childIDPtr = bigintToI53Checked(childIDPtr);
  let name = UTF8ToString(namePtr);
  let childID = await wasmfsOPFSGetOrCreateFile(parent, name, true);
  (growMemViews(), HEAP32)[((childIDPtr) >> 2)] = childID;
  wasmfsOPFSProxyFinish(ctx);
}

async function __wasmfs_opfs_move_file(ctx, fileID, newParentID, namePtr, errPtr) {
  ctx = bigintToI53Checked(ctx);
  namePtr = bigintToI53Checked(namePtr);
  errPtr = bigintToI53Checked(errPtr);
  let name = UTF8ToString(namePtr);
  let fileHandle = wasmfsOPFSFileHandles.get(fileID);
  let newDirHandle = wasmfsOPFSDirectoryHandles.get(newParentID);
  try {
    await fileHandle.move(newDirHandle, name);
  } catch {
    let err = -29;
    (growMemViews(), HEAP32)[((errPtr) >> 2)] = err;
  }
  wasmfsOPFSProxyFinish(ctx);
}

async function __wasmfs_opfs_open_access(ctx, fileID, accessIDPtr) {
  ctx = bigintToI53Checked(ctx);
  accessIDPtr = bigintToI53Checked(accessIDPtr);
  let fileHandle = wasmfsOPFSFileHandles.get(fileID);
  let accessID;
  try {
    let accessHandle;
    // TODO: Remove this once the Access Handles API has settled.
    // TODO: Closure is confused by this code that supports two versions of
    //       the same API, so suppress type checking on it.
    /** @suppress {checkTypes} */ var len = FileSystemFileHandle.prototype.createSyncAccessHandle.length;
    if (len == 0) {
      accessHandle = await fileHandle.createSyncAccessHandle();
    } else {
      accessHandle = await fileHandle.createSyncAccessHandle({
        mode: "in-place"
      });
    }
    accessID = wasmfsOPFSAccessHandles.allocate(accessHandle);
  } catch (e) {
    // TODO: Presumably only one of these will appear in the final API?
    if (e.name === "InvalidStateError" || e.name === "NoModificationAllowedError") {
      accessID = -2;
    } else {
      err("unexpected error:", e, e.stack);
      accessID = -29;
    }
  }
  (growMemViews(), HEAP32)[((accessIDPtr) >> 2)] = accessID;
  wasmfsOPFSProxyFinish(ctx);
}

async function __wasmfs_opfs_open_blob(ctx, fileID, blobIDPtr) {
  ctx = bigintToI53Checked(ctx);
  blobIDPtr = bigintToI53Checked(blobIDPtr);
  let fileHandle = wasmfsOPFSFileHandles.get(fileID);
  let blobID;
  try {
    let blob = await fileHandle.getFile();
    blobID = wasmfsOPFSBlobs.allocate(blob);
  } catch (e) {
    if (e.name === "NotAllowedError") {
      blobID = -2;
    } else {
      err("unexpected error:", e, e.stack);
      blobID = -29;
    }
  }
  (growMemViews(), HEAP32)[((blobIDPtr) >> 2)] = blobID;
  wasmfsOPFSProxyFinish(ctx);
}

function __wasmfs_opfs_read_access(accessID, bufPtr, len, pos) {
  bufPtr = bigintToI53Checked(bufPtr);
  pos = bigintToI53Checked(pos);
  let accessHandle = wasmfsOPFSAccessHandles.get(accessID);
  let data = (growMemViews(), HEAPU8).subarray(bufPtr, bufPtr + len);
  try {
    return accessHandle.read(data, {
      at: pos
    });
  } catch (e) {
    if (e.name == "TypeError") {
      return -28;
    }
    err("unexpected error:", e, e.stack);
    return -29;
  }
}

async function __wasmfs_opfs_read_blob(ctx, blobID, bufPtr, len, pos, nreadPtr) {
  ctx = bigintToI53Checked(ctx);
  bufPtr = bigintToI53Checked(bufPtr);
  pos = bigintToI53Checked(pos);
  nreadPtr = bigintToI53Checked(nreadPtr);
  let blob = wasmfsOPFSBlobs.get(blobID);
  let slice = blob.slice(pos, pos + len);
  let nread = 0;
  try {
    // TODO: Use ReadableStreamBYOBReader once
    // https://bugs.chromium.org/p/chromium/issues/detail?id=1189621 is
    // resolved.
    let buf = await slice.arrayBuffer();
    let data = new Uint8Array(buf);
    (growMemViews(), HEAPU8).set(data, bufPtr);
    nread += data.length;
  } catch (e) {
    if (e instanceof RangeError) {
      nread = -21;
    } else {
      err("unexpected error:", e, e.stack);
      nread = -29;
    }
  }
  (growMemViews(), HEAP32)[((nreadPtr) >> 2)] = nread;
  wasmfsOPFSProxyFinish(ctx);
}

async function __wasmfs_opfs_remove_child(ctx, dirID, namePtr, errPtr) {
  ctx = bigintToI53Checked(ctx);
  namePtr = bigintToI53Checked(namePtr);
  errPtr = bigintToI53Checked(errPtr);
  let name = UTF8ToString(namePtr);
  let dirHandle = wasmfsOPFSDirectoryHandles.get(dirID);
  try {
    await dirHandle.removeEntry(name);
  } catch {
    let err = -29;
    (growMemViews(), HEAP32)[((errPtr) >> 2)] = err;
  }
  wasmfsOPFSProxyFinish(ctx);
}

async function __wasmfs_opfs_set_size_access(ctx, accessID, size, errPtr) {
  ctx = bigintToI53Checked(ctx);
  size = bigintToI53Checked(size);
  errPtr = bigintToI53Checked(errPtr);
  let accessHandle = wasmfsOPFSAccessHandles.get(accessID);
  try {
    await accessHandle.truncate(size);
  } catch {
    let err = -29;
    (growMemViews(), HEAP32)[((errPtr) >> 2)] = err;
  }
  wasmfsOPFSProxyFinish(ctx);
}

async function __wasmfs_opfs_set_size_file(ctx, fileID, size, errPtr) {
  ctx = bigintToI53Checked(ctx);
  size = bigintToI53Checked(size);
  errPtr = bigintToI53Checked(errPtr);
  let fileHandle = wasmfsOPFSFileHandles.get(fileID);
  try {
    let writable = await fileHandle.createWritable({
      keepExistingData: true
    });
    await writable.truncate(size);
    await writable.close();
  } catch {
    let err = -29;
    (growMemViews(), HEAP32)[((errPtr) >> 2)] = err;
  }
  wasmfsOPFSProxyFinish(ctx);
}

function __wasmfs_opfs_write_access(accessID, bufPtr, len, pos) {
  bufPtr = bigintToI53Checked(bufPtr);
  pos = bigintToI53Checked(pos);
  let accessHandle = wasmfsOPFSAccessHandles.get(accessID);
  let data = (growMemViews(), HEAPU8).subarray(bufPtr, bufPtr + len);
  try {
    return accessHandle.write(data, {
      at: pos
    });
  } catch (e) {
    if (e.name == "TypeError") {
      return -28;
    }
    err("unexpected error:", e, e.stack);
    return -29;
  }
}

var FS_stdin_getChar_buffer = [];

/** @type {function(string, boolean=, number=)} */ var intArrayFromString = (stringy, dontAddNull, length) => {
  var len = length > 0 ? length : lengthBytesUTF8(stringy) + 1;
  var u8array = new Array(len);
  var numBytesWritten = stringToUTF8Array(stringy, u8array, 0, u8array.length);
  if (dontAddNull) u8array.length = numBytesWritten;
  return u8array;
};

var FS_stdin_getChar = () => {
  if (!FS_stdin_getChar_buffer.length) {
    var result = null;
    if (globalThis.window?.prompt) {
      // Browser.
      result = window.prompt("Input: ");
      // returns null on cancel
      if (result !== null) {
        result += "\n";
      }
    } else {}
    if (!result) {
      return null;
    }
    FS_stdin_getChar_buffer = intArrayFromString(result, true);
  }
  return FS_stdin_getChar_buffer.shift();
};

var __wasmfs_stdin_get_char = () => {
  // Return the read character, or -1 to indicate EOF.
  var c = FS_stdin_getChar();
  if (typeof c === "number") {
    return c;
  }
  return -1;
};

var __wasmfs_thread_utils_heartbeat = function(queue) {
  queue = bigintToI53Checked(queue);
  var intervalID = setInterval(() => {
    if (ABORT) {
      clearInterval(intervalID);
    } else {
      _emscripten_proxy_execute_queue(queue);
    }
  }, 50);
};

var _emscripten_get_now = () => performance.timeOrigin + performance.now();

var _emscripten_date_now = () => Date.now();

var nowIsMonotonic = 1;

var checkWasiClock = clock_id => clock_id >= 0 && clock_id <= 3;

function _clock_time_get(clk_id, ignored_precision, ptime) {
  ignored_precision = bigintToI53Checked(ignored_precision);
  ptime = bigintToI53Checked(ptime);
  if (!checkWasiClock(clk_id)) {
    return 28;
  }
  var now;
  // all wasi clocks but realtime are monotonic
  if (clk_id === 0) {
    now = _emscripten_date_now();
  } else if (nowIsMonotonic) {
    now = _emscripten_get_now();
  } else {
    return 52;
  }
  // "now" is in ms, and wasi times are in ns.
  var nsec = Math.round(now * 1e3 * 1e3);
  (growMemViews(), HEAP64)[((ptime) >> 3)] = BigInt(nsec);
  return 0;
}

var readEmAsmArgsArray = [];

var readEmAsmArgs = (sigPtr, buf) => {
  // Nobody should have mutated _readEmAsmArgsArray underneath us to be something else than an array.
  assert(Array.isArray(readEmAsmArgsArray));
  // The input buffer is allocated on the stack, so it must be stack-aligned.
  assert(buf % 16 == 0);
  readEmAsmArgsArray.length = 0;
  var ch;
  // Most arguments are i32s, so shift the buffer pointer so it is a plain
  // index into HEAP32.
  while (ch = (growMemViews(), HEAPU8)[sigPtr++]) {
    var chr = String.fromCharCode(ch);
    var validChars = [ "d", "f", "i", "p" ];
    // In WASM_BIGINT mode we support passing i64 values as bigint.
    validChars.push("j");
    assert(validChars.includes(chr), `Invalid character ${ch}("${chr}") in readEmAsmArgs! Use only [${validChars}], and do not specify "v" for void return argument.`);
    // Floats are always passed as doubles, so all types except for 'i'
    // are 8 bytes and require alignment.
    var wide = (ch != 105);
    buf += wide && (buf % 8) ? 4 : 0;
    readEmAsmArgsArray.push(// Special case for pointers under wasm64 or CAN_ADDRESS_2GB mode.
    ch == 112 ? Number((growMemViews(), HEAPU64)[((buf) >> 3)]) : ch == 106 ? (growMemViews(), 
    HEAP64)[((buf) >> 3)] : ch == 105 ? (growMemViews(), HEAP32)[((buf) >> 2)] : (growMemViews(), 
    HEAPF64)[((buf) >> 3)]);
    buf += wide ? 8 : 4;
  }
  return readEmAsmArgsArray;
};

var runMainThreadEmAsm = (emAsmAddr, sigPtr, argbuf, sync) => {
  var args = readEmAsmArgs(sigPtr, argbuf);
  if (ENVIRONMENT_IS_PTHREAD) {
    // EM_ASM functions are variadic, receiving the actual arguments as a buffer
    // in memory. the last parameter (argBuf) points to that data. We need to
    // always un-variadify that, *before proxying*, as in the async case this
    // is a stack allocation that LLVM made, which may go away before the main
    // thread gets the message. For that reason we handle proxying *after* the
    // call to readEmAsmArgs, and therefore we do that manually here instead
    // of using __proxy. (And for simplicity, do the same in the sync
    // case as well, even though it's not strictly necessary, to keep the two
    // code paths as similar as possible on both sides.)
    return proxyToMainThread(0, emAsmAddr, sync, ...args);
  }
  assert(ASM_CONSTS.hasOwnProperty(emAsmAddr), `No EM_ASM constant found at address ${emAsmAddr}.  The loaded WebAssembly file is likely out of sync with the generated JavaScript.`);
  return ASM_CONSTS[emAsmAddr](...args);
};

function _emscripten_asm_const_async_on_main_thread(emAsmAddr, sigPtr, argbuf) {
  emAsmAddr = bigintToI53Checked(emAsmAddr);
  sigPtr = bigintToI53Checked(sigPtr);
  argbuf = bigintToI53Checked(argbuf);
  return runMainThreadEmAsm(emAsmAddr, sigPtr, argbuf, 0);
}

var runEmAsmFunction = (code, sigPtr, argbuf) => {
  var args = readEmAsmArgs(sigPtr, argbuf);
  assert(ASM_CONSTS.hasOwnProperty(code), `No EM_ASM constant found at address ${code}.  The loaded WebAssembly file is likely out of sync with the generated JavaScript.`);
  return ASM_CONSTS[code](...args);
};

function _emscripten_asm_const_double(code, sigPtr, argbuf) {
  code = bigintToI53Checked(code);
  sigPtr = bigintToI53Checked(sigPtr);
  argbuf = bigintToI53Checked(argbuf);
  return runEmAsmFunction(code, sigPtr, argbuf);
}

function _emscripten_asm_const_int_sync_on_main_thread(emAsmAddr, sigPtr, argbuf) {
  emAsmAddr = bigintToI53Checked(emAsmAddr);
  sigPtr = bigintToI53Checked(sigPtr);
  argbuf = bigintToI53Checked(argbuf);
  return runMainThreadEmAsm(emAsmAddr, sigPtr, argbuf, 1);
}

function _emscripten_asm_const_double_sync_on_main_thread(a1, a2, a3) {
  a1 = bigintToI53Checked(a1);
  a2 = bigintToI53Checked(a2);
  a3 = bigintToI53Checked(a3);
  return _emscripten_asm_const_int_sync_on_main_thread(a1, a2, a3);
}

function _emscripten_asm_const_int(code, sigPtr, argbuf) {
  code = bigintToI53Checked(code);
  sigPtr = bigintToI53Checked(sigPtr);
  argbuf = bigintToI53Checked(argbuf);
  return runEmAsmFunction(code, sigPtr, argbuf);
}

var _emscripten_check_blocking_allowed = () => {
  if (ENVIRONMENT_IS_WORKER) return;
  // Blocking in a worker/pthread is fine.
  warnOnce("Blocking on the main thread is very dangerous, see https://emscripten.org/docs/porting/pthreads.html#blocking-on-the-main-browser-thread");
};

function _emscripten_console_log(str) {
  str = bigintToI53Checked(str);
  console.log(UTF8ToString(str));
}

function _emscripten_err(str) {
  str = bigintToI53Checked(str);
  return err(UTF8ToString(str));
}

var _emscripten_exit_with_live_runtime = () => {
  runtimeKeepalivePush();
  throw "unwind";
};

var jsStackTrace = () => (new Error).stack.toString();

/** @param {number=} flags */ var getCallstack = flags => {
  var callstack = jsStackTrace();
  if (flags & 8) {
    warnOnce("emscripten_log with EM_LOG_C_STACK no longer has any effect");
  }
  // Process all lines:
  var lines = callstack.split("\n");
  callstack = "";
  // Extract components of form:
  // '       Object._main@http://server.com:4324:12'
  var firefoxRe = new RegExp("\\s*(.*?)@(.*?):([0-9]+):([0-9]+)");
  // Extract components of form:
  // '    at Object._main (http://server.com/file.html:4324:12)'
  var chromeRe = new RegExp("\\s*at (.*?) \\((.*):(.*):(.*)\\)");
  for (var line of lines) {
    var symbolName = "";
    var file = "";
    var lineno = 0;
    var column = 0;
    var parts = chromeRe.exec(line);
    if (parts?.length == 5) {
      symbolName = parts[1];
      file = parts[2];
      lineno = parts[3];
      column = parts[4];
    } else {
      parts = firefoxRe.exec(line);
      if (parts?.length >= 4) {
        symbolName = parts[1];
        file = parts[2];
        lineno = parts[3];
        // Old Firefox doesn't carry column information, but in new FF30, it
        // is present. See https://bugzil.la/762556
        column = parts[4] | 0;
      } else {
        // Was not able to extract this line for demangling/sourcemapping
        // purposes. Output it as-is.
        callstack += line + "\n";
        continue;
      }
    }
    // Find the symbols in the callstack that corresponds to the functions that
    // report callstack information, and remove everything up to these from the
    // output.
    if (symbolName == "_emscripten_log" || symbolName == "_emscripten_get_callstack") {
      callstack = "";
      continue;
    }
    if ((flags & 24)) {
      if (flags & 64) {
        file = file.substring(file.replace(/\\/g, "/").lastIndexOf("/") + 1);
      }
      callstack += `    at ${symbolName} (${file}:${lineno}:${column})\n`;
    }
  }
  // Trim extra whitespace at the end of the output.
  callstack = callstack.replace(/\s+$/, "");
  return callstack;
};

function _emscripten_get_callstack(flags, str, maxbytes) {
  str = bigintToI53Checked(str);
  var callstack = getCallstack(flags);
  // User can query the required amount of bytes to hold the callstack.
  if (!str || maxbytes <= 0) {
    return lengthBytesUTF8(callstack) + 1;
  }
  // Output callstack string as C string to HEAP.
  var bytesWrittenExcludingNull = stringToUTF8(callstack, str, maxbytes);
  // Return number of bytes written, including null.
  return bytesWrittenExcludingNull + 1;
}

var getHeapMax = () => // Stay one Wasm page short of 4GB: while e.g. Chrome is able to allocate
// full 4GB Wasm memories, the size will wrap back to 0 bytes in Wasm side
// for any code that deals with heap sizes, which would require special
// casing all heap size related code to treat 0 specially.
4294901760;

var _emscripten_get_heap_max = () => BigInt(getHeapMax());

var fillVisibilityChangeEventData = eventStruct => {
  var visibilityStates = [ "hidden", "visible", "prerender", "unloaded" ];
  var visibilityState = visibilityStates.indexOf(document.visibilityState);
  // Assigning a boolean to HEAP32 with expected type coercion.
  /** @suppress{checkTypes} */ (growMemViews(), HEAP8)[eventStruct] = document.hidden;
  (growMemViews(), HEAP32)[(((eventStruct) + (4)) >> 2)] = visibilityState;
};

function _emscripten_get_visibility_status(visibilityStatus) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(3, 0, 1, visibilityStatus);
  visibilityStatus = bigintToI53Checked(visibilityStatus);
  if (typeof document.visibilityState == "undefined" && typeof document.hidden == "undefined") {
    return -1;
  }
  fillVisibilityChangeEventData(visibilityStatus);
  return 0;
}

var _emscripten_has_asyncify = () => 0;

var _emscripten_num_logical_cores = () => navigator["hardwareConcurrency"];

function _emscripten_out(str) {
  str = bigintToI53Checked(str);
  return out(UTF8ToString(str));
}

var _emscripten_request_animation_frame = function(cb, userData) {
  cb = bigintToI53Checked(cb);
  userData = bigintToI53Checked(userData);
  return requestAnimationFrame(timeStamp => ((a1, a2) => getWasmTableEntry(cb).call(null, a1, BigInt(a2)))(timeStamp, userData));
};

var alignMemory = (size, alignment) => {
  assert(alignment, "alignment argument is required");
  return Math.ceil(size / alignment) * alignment;
};

var growMemory = size => {
  var oldHeapSize = wasmMemory.buffer.byteLength;
  var pages = ((size - oldHeapSize + 65535) / 65536) | 0;
  try {
    // round size grow request up to wasm page size (fixed 64KB per spec)
    wasmMemory.grow(pages);
    // .grow() takes a delta compared to the previous size
    updateMemoryViews();
    return 1;
  } catch (e) {
    err(`growMemory: Attempted to grow heap from ${oldHeapSize} bytes to ${size} bytes, but got error: ${e}`);
  }
};

function _emscripten_resize_heap(requestedSize) {
  requestedSize = bigintToI53Checked(requestedSize);
  var oldSize = (growMemViews(), HEAPU8).length;
  // With multithreaded builds, races can happen (another thread might increase the size
  // in between), so return a failure, and let the caller retry.
  if (requestedSize <= oldSize) {
    return false;
  }
  // Memory resize rules:
  // 1.  Always increase heap size to at least the requested size, rounded up
  //     to next page multiple.
  // 2a. If MEMORY_GROWTH_LINEAR_STEP == -1, excessively resize the heap
  //     geometrically: increase the heap size according to
  //     MEMORY_GROWTH_GEOMETRIC_STEP factor (default +20%), At most
  //     overreserve by MEMORY_GROWTH_GEOMETRIC_CAP bytes (default 96MB).
  // 2b. If MEMORY_GROWTH_LINEAR_STEP != -1, excessively resize the heap
  //     linearly: increase the heap size by at least
  //     MEMORY_GROWTH_LINEAR_STEP bytes.
  // 3.  Max size for the heap is capped at 2048MB-WASM_PAGE_SIZE, or by
  //     MAXIMUM_MEMORY, or by ASAN limit, depending on which is smallest
  // 4.  If we were unable to allocate as much memory, it may be due to
  //     over-eager decision to excessively reserve due to (3) above.
  //     Hence if an allocation fails, cut down on the amount of excess
  //     growth, in an attempt to succeed to perform a smaller allocation.
  // A limit is set for how much we can grow. We should not exceed that
  // (the wasm binary specifies it, so if we tried, we'd fail anyhow).
  var maxHeapSize = getHeapMax();
  if (requestedSize > maxHeapSize) {
    err(`Cannot enlarge memory, requested ${requestedSize} bytes, but the limit is ${maxHeapSize} bytes!`);
    return false;
  }
  // Loop through potential heap size increases. If we attempt a too eager
  // reservation that fails, cut down on the attempted size and reserve a
  // smaller bump instead. (max 3 times, chosen somewhat arbitrarily)
  for (var cutDown = 1; cutDown <= 4; cutDown *= 2) {
    var overGrownHeapSize = oldSize * (1 + .2 / cutDown);
    // ensure geometric growth
    // but limit overreserving (default to capping at +96MB overgrowth at most)
    overGrownHeapSize = Math.min(overGrownHeapSize, requestedSize + 100663296);
    var newSize = Math.min(maxHeapSize, alignMemory(Math.max(requestedSize, overGrownHeapSize), 65536));
    var replacement = growMemory(newSize);
    if (replacement) {
      return true;
    }
  }
  err(`Failed to grow the heap from ${oldSize} bytes to ${newSize} bytes, not enough memory!`);
  return false;
}

/** @returns {number} */ var convertFrameToPC = frame => {
  var match;
  if (match = /\bwasm-function\[\d+\]:(0x[0-9a-f]+)/.exec(frame)) {
    // Wasm engines give the binary offset directly, so we use that as return address
    return +match[1];
  } else if (match = /\bwasm-function\[(\d+)\]:(\d+)/.exec(frame)) {
    // Older versions of v8 (e.g node v10) give function index and offset in
    // the function.  That format is not supported since it does not provide
    // the information we need to map the frame to a global program counter.
    warnOnce("legacy backtrace format detected, this version of v8 is no longer supported by the emscripten backtrace mechanism");
  } else if (match = /:(\d+):\d+(?:\)|$)/.exec(frame)) {
    // If we are in js, we can use the js line number as the "return address".
    // This should work for wasm2js.  We tag the high bit to distinguish this
    // from wasm addresses.
    return 2147483648 | +match[1];
  }
  // return 0 if we can't find any
  return 0;
};

var _emscripten_return_address = function(level) {
  var ret = (() => {
    var callstack = jsStackTrace().split("\n");
    if (callstack[0] == "Error") {
      callstack.shift();
    }
    // skip this function and the caller to get caller's return address
    // MEMORY64 injects an extra wrapper within emscripten_return_address
    // to handle BigInt conversions.
    var caller = callstack[level + 4];
    return convertFrameToPC(caller);
  })();
  return BigInt(ret);
};

var _emscripten_runtime_keepalive_check = keepRuntimeAlive;

var onExits = [];

var addOnExit = cb => onExits.push(cb);

var JSEvents = {
  removeAllEventListeners() {
    while (JSEvents.eventHandlers.length) {
      JSEvents._removeHandler(JSEvents.eventHandlers.length - 1);
    }
    JSEvents.deferredCalls = [];
  },
  registerRemoveEventListeners() {
    if (!JSEvents.removeEventListenersRegistered) {
      addOnExit(JSEvents.removeAllEventListeners);
      JSEvents.removeEventListenersRegistered = true;
    }
  },
  inEventHandler: 0,
  deferredCalls: [],
  deferCall(targetFunction, precedence, argsList) {
    function arraysHaveEqualContent(arrA, arrB) {
      if (arrA.length != arrB.length) return false;
      for (var i = 0; i < arrA.length; i++) {
        if (arrA[i] != arrB[i]) return false;
      }
      return true;
    }
    // Test if the given call was already queued, and if so, don't add it again.
    for (var call of JSEvents.deferredCalls) {
      if (call.targetFunction == targetFunction && arraysHaveEqualContent(call.argsList, argsList)) {
        return;
      }
    }
    JSEvents.deferredCalls.push({
      targetFunction,
      precedence,
      argsList
    });
    JSEvents.deferredCalls.sort((x, y) => x.precedence - y.precedence);
  },
  removeDeferredCalls(targetFunction) {
    JSEvents.deferredCalls = JSEvents.deferredCalls.filter(call => call.targetFunction != targetFunction);
  },
  canPerformEventHandlerRequests() {
    // Browsers that support navigator.userActivation.isActive: https://developer.mozilla.org/en-US/docs/Web/API/UserActivation/isActive
    if (navigator.userActivation) {
      // Verify against transient activation status from UserActivation API
      // whether it is possible to perform a request here without needing to defer. See
      // https://developer.mozilla.org/en-US/docs/Web/Security/User_activation#transient_activation
      // and https://caniuse.com/mdn-api_useractivation
      return navigator.userActivation.isActive;
    }
    return JSEvents.inEventHandler && JSEvents.currentEventHandler.allowsDeferredCalls;
  },
  runDeferredCalls() {
    if (!JSEvents.canPerformEventHandlerRequests()) {
      return;
    }
    var deferredCalls = JSEvents.deferredCalls;
    JSEvents.deferredCalls = [];
    for (var call of deferredCalls) {
      call.targetFunction(...call.argsList);
    }
  },
  eventHandlers: [],
  removeAllHandlersOnTarget: (target, eventTypeString) => {
    for (var i = 0; i < JSEvents.eventHandlers.length; ++i) {
      if (JSEvents.eventHandlers[i].target == target && (!eventTypeString || eventTypeString == JSEvents.eventHandlers[i].eventTypeString)) {
        JSEvents._removeHandler(i--);
      }
    }
  },
  _removeHandler(i) {
    var h = JSEvents.eventHandlers[i];
    h.target.removeEventListener(h.eventTypeString, h.eventListenerFunc, h.useCapture);
    JSEvents.eventHandlers.splice(i, 1);
  },
  registerOrRemoveHandler(eventHandler) {
    if (!eventHandler.target) {
      err("registerOrRemoveHandler: the target element for event handler registration does not exist, when processing the following event handler registration:");
      console.dir(eventHandler);
      return -4;
    }
    if (eventHandler.callbackfunc) {
      eventHandler.eventListenerFunc = function(event) {
        // Increment nesting count for the event handler.
        ++JSEvents.inEventHandler;
        JSEvents.currentEventHandler = eventHandler;
        // Process any old deferred calls the user has placed.
        JSEvents.runDeferredCalls();
        // Process the actual event, calls back to user C code handler.
        eventHandler.handlerFunc(event);
        // Process any new deferred calls that were placed right now from this event handler.
        JSEvents.runDeferredCalls();
        // Out of event handler - restore nesting count.
        --JSEvents.inEventHandler;
      };
      eventHandler.target.addEventListener(eventHandler.eventTypeString, eventHandler.eventListenerFunc, eventHandler.useCapture);
      JSEvents.eventHandlers.push(eventHandler);
      JSEvents.registerRemoveEventListeners();
    } else {
      for (var i = 0; i < JSEvents.eventHandlers.length; ++i) {
        if (JSEvents.eventHandlers[i].target == eventHandler.target && JSEvents.eventHandlers[i].eventTypeString == eventHandler.eventTypeString) {
          JSEvents._removeHandler(i--);
        }
      }
    }
    return 0;
  },
  removeSingleHandler(eventHandler) {
    let success = false;
    for (let i = 0; i < JSEvents.eventHandlers.length; ++i) {
      const handler = JSEvents.eventHandlers[i];
      if (handler.target === eventHandler.target && handler.eventTypeId === eventHandler.eventTypeId && handler.callbackfunc === eventHandler.callbackfunc && handler.userData === eventHandler.userData) {
        // in some very rare cases (ex: Safari / fullscreen events), there is more than 1 handler (eventTypeString is different)
        JSEvents._removeHandler(i--);
        success = true;
      }
    }
    return success ? 0 : -5;
  },
  getTargetThreadForEventCallback(targetThread) {
    switch (targetThread) {
     case 1:
      // The event callback for the current event should be called on the
      // main browser thread. (0 == don't proxy)
      return 0;

     case 2:
      // The event callback for the current event should be backproxied to
      // the thread that is registering the event.
      // This can be 0 in the case that the caller uses
      // EM_CALLBACK_THREAD_CONTEXT_CALLING_THREAD but on the main thread
      // itself.
      return PThread.currentProxiedOperationCallerThread;

     default:
      // The event callback for the current event should be proxied to the
      // given specific thread.
      return targetThread;
    }
  },
  getNodeNameForTarget(target) {
    if (target == window) return "#window";
    if (target == screen) return "#screen";
    return target?.nodeName ?? "";
  },
  fullscreenEnabled() {
    return document.fullscreenEnabled ?? document.webkitFullscreenEnabled;
  }
};

var maybeCStringToJsString = cString => cString > 2 ? UTF8ToString(cString) : cString;

/** @type {Object} */ var specialHTMLTargets = [ 0, globalThis.document ?? 0, globalThis.window ?? 0 ];

var findEventTarget = target => {
  target = maybeCStringToJsString(target);
  var domElement = specialHTMLTargets[target] || globalThis.document?.querySelector(target);
  return domElement;
};

var registerFocusEventCallback = (target, userData, useCapture, callbackfunc, eventTypeId, eventTypeString, targetThread) => {
  targetThread = JSEvents.getTargetThreadForEventCallback(targetThread);
  var eventSize = 256;
  JSEvents.focusEvent ||= _malloc(eventSize);
  var focusEventHandlerFunc = e => {
    var nodeName = JSEvents.getNodeNameForTarget(e.target);
    var id = e.target.id ?? "";
    var focusEvent = JSEvents.focusEvent;
    stringToUTF8(nodeName, focusEvent + 0, 128);
    stringToUTF8(id, focusEvent + 128, 128);
    if (targetThread) __emscripten_run_callback_on_thread(targetThread, callbackfunc, eventTypeId, focusEvent, eventSize, userData); else if (((a1, a2, a3) => getWasmTableEntry(callbackfunc).call(null, a1, BigInt(a2), BigInt(a3)))(eventTypeId, focusEvent, userData)) e.preventDefault();
  };
  var eventHandler = {
    target: findEventTarget(target),
    eventTypeString,
    eventTypeId,
    userData,
    callbackfunc,
    handlerFunc: focusEventHandlerFunc,
    useCapture
  };
  return JSEvents.registerOrRemoveHandler(eventHandler);
};

function _emscripten_set_blur_callback_on_thread(target, userData, useCapture, callbackfunc, targetThread) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(4, 0, 1, target, userData, useCapture, callbackfunc, targetThread);
  target = bigintToI53Checked(target);
  userData = bigintToI53Checked(userData);
  callbackfunc = bigintToI53Checked(callbackfunc);
  targetThread = bigintToI53Checked(targetThread);
  return registerFocusEventCallback(target, userData, useCapture, callbackfunc, 12, "blur", targetThread);
}

var setImmediateWrapped = func => {
  setImmediateWrapped.mapping ||= [];
  var id = setImmediateWrapped.mapping.length;
  setImmediateWrapped.mapping[id] = setImmediate(() => {
    setImmediateWrapped.mapping[id] = undefined;
    func();
  });
  return id;
};

var clearImmediateWrapped = id => {
  var handle = setImmediateWrapped.mapping[id];
  if (!handle) return false;
  clearImmediate(handle);
  setImmediateWrapped.mapping[id] = undefined;
  return true;
};

var emClearImmediate;

var emSetImmediate;

var _emscripten_set_immediate = function(cb, userData) {
  cb = bigintToI53Checked(cb);
  userData = bigintToI53Checked(userData);
  runtimeKeepalivePush();
  return emSetImmediate(() => {
    runtimeKeepalivePop();
    callUserCallback(() => (a1 => getWasmTableEntry(cb).call(null, BigInt(a1)))(userData));
  });
};

var registerKeyEventCallback = (target, userData, useCapture, callbackfunc, eventTypeId, eventTypeString, targetThread) => {
  targetThread = JSEvents.getTargetThreadForEventCallback(targetThread);
  var eventSize = 160;
  JSEvents.keyEvent ||= _malloc(eventSize);
  var keyEventHandlerFunc = e => {
    assert(e);
    var keyEventData = JSEvents.keyEvent;
    (growMemViews(), HEAPF64)[((keyEventData) >> 3)] = e.timeStamp;
    var idx = ((keyEventData) >> 2);
    (growMemViews(), HEAP32)[idx + 2] = e.location;
    (growMemViews(), HEAP8)[keyEventData + 12] = e.ctrlKey;
    (growMemViews(), HEAP8)[keyEventData + 13] = e.shiftKey;
    (growMemViews(), HEAP8)[keyEventData + 14] = e.altKey;
    (growMemViews(), HEAP8)[keyEventData + 15] = e.metaKey;
    (growMemViews(), HEAP8)[keyEventData + 16] = e.repeat;
    (growMemViews(), HEAP32)[idx + 5] = e.charCode;
    (growMemViews(), HEAP32)[idx + 6] = e.keyCode;
    (growMemViews(), HEAP32)[idx + 7] = e.which;
    stringToUTF8(e.key ?? "", keyEventData + 32, 32);
    stringToUTF8(e.code ?? "", keyEventData + 64, 32);
    stringToUTF8(e.char ?? "", keyEventData + 96, 32);
    stringToUTF8(e.locale ?? "", keyEventData + 128, 32);
    if (targetThread) __emscripten_run_callback_on_thread(targetThread, callbackfunc, eventTypeId, keyEventData, eventSize, userData); else if (((a1, a2, a3) => getWasmTableEntry(callbackfunc).call(null, a1, BigInt(a2), BigInt(a3)))(eventTypeId, keyEventData, userData)) e.preventDefault();
  };
  var eventHandler = {
    target: findEventTarget(target),
    eventTypeString,
    eventTypeId,
    userData,
    callbackfunc,
    handlerFunc: keyEventHandlerFunc,
    useCapture
  };
  return JSEvents.registerOrRemoveHandler(eventHandler);
};

function _emscripten_set_keydown_callback_on_thread(target, userData, useCapture, callbackfunc, targetThread) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(5, 0, 1, target, userData, useCapture, callbackfunc, targetThread);
  target = bigintToI53Checked(target);
  userData = bigintToI53Checked(userData);
  callbackfunc = bigintToI53Checked(callbackfunc);
  targetThread = bigintToI53Checked(targetThread);
  return registerKeyEventCallback(target, userData, useCapture, callbackfunc, 2, "keydown", targetThread);
}

function _emscripten_set_keyup_callback_on_thread(target, userData, useCapture, callbackfunc, targetThread) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(6, 0, 1, target, userData, useCapture, callbackfunc, targetThread);
  target = bigintToI53Checked(target);
  userData = bigintToI53Checked(userData);
  callbackfunc = bigintToI53Checked(callbackfunc);
  targetThread = bigintToI53Checked(targetThread);
  return registerKeyEventCallback(target, userData, useCapture, callbackfunc, 3, "keyup", targetThread);
}

var getBoundingClientRect = e => specialHTMLTargets.indexOf(e) < 0 ? e.getBoundingClientRect() : {
  "left": 0,
  "top": 0
};

/** @type {!Int16Array} */ var HEAP16;

var fillMouseEventData = (eventStruct, e, target) => {
  assert(eventStruct % 4 == 0);
  (growMemViews(), HEAPF64)[((eventStruct) >> 3)] = e.timeStamp;
  var idx = ((eventStruct) >> 2);
  (growMemViews(), HEAP32)[idx + 2] = e.screenX;
  (growMemViews(), HEAP32)[idx + 3] = e.screenY;
  (growMemViews(), HEAP32)[idx + 4] = e.clientX;
  (growMemViews(), HEAP32)[idx + 5] = e.clientY;
  (growMemViews(), HEAP8)[eventStruct + 24] = e.ctrlKey;
  (growMemViews(), HEAP8)[eventStruct + 25] = e.shiftKey;
  (growMemViews(), HEAP8)[eventStruct + 26] = e.altKey;
  (growMemViews(), HEAP8)[eventStruct + 27] = e.metaKey;
  (growMemViews(), HEAP16)[idx * 2 + 14] = e.button;
  (growMemViews(), HEAP16)[idx * 2 + 15] = e.buttons;
  (growMemViews(), HEAP32)[idx + 8] = e.movementX;
  (growMemViews(), HEAP32)[idx + 9] = e.movementY;
  // Note: rect contains doubles (truncated to placate SAFE_HEAP, which is the same behaviour when writing to HEAP32 anyway)
  var rect = getBoundingClientRect(target);
  (growMemViews(), HEAP32)[idx + 10] = e.clientX - (rect.left | 0);
  (growMemViews(), HEAP32)[idx + 11] = e.clientY - (rect.top | 0);
};

var registerMouseEventCallback = (target, userData, useCapture, callbackfunc, eventTypeId, eventTypeString, targetThread) => {
  targetThread = JSEvents.getTargetThreadForEventCallback(targetThread);
  var eventSize = 64;
  JSEvents.mouseEvent ||= _malloc(eventSize);
  target = findEventTarget(target);
  var mouseEventHandlerFunc = e => {
    // TODO: Make this access thread safe, or this could update live while app is reading it.
    fillMouseEventData(JSEvents.mouseEvent, e, target);
    if (targetThread) {
      __emscripten_run_callback_on_thread(targetThread, callbackfunc, eventTypeId, JSEvents.mouseEvent, eventSize, userData);
    } else if (((a1, a2, a3) => getWasmTableEntry(callbackfunc).call(null, a1, BigInt(a2), BigInt(a3)))(eventTypeId, JSEvents.mouseEvent, userData)) e.preventDefault();
  };
  var eventHandler = {
    target,
    allowsDeferredCalls: eventTypeString != "mousemove" && eventTypeString != "mouseenter" && eventTypeString != "mouseleave",
    // Mouse move events do not allow fullscreen/pointer lock requests to be handled in them!
    eventTypeString,
    eventTypeId,
    userData,
    callbackfunc,
    handlerFunc: mouseEventHandlerFunc,
    useCapture
  };
  return JSEvents.registerOrRemoveHandler(eventHandler);
};

function _emscripten_set_mousedown_callback_on_thread(target, userData, useCapture, callbackfunc, targetThread) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(7, 0, 1, target, userData, useCapture, callbackfunc, targetThread);
  target = bigintToI53Checked(target);
  userData = bigintToI53Checked(userData);
  callbackfunc = bigintToI53Checked(callbackfunc);
  targetThread = bigintToI53Checked(targetThread);
  return registerMouseEventCallback(target, userData, useCapture, callbackfunc, 5, "mousedown", targetThread);
}

function _emscripten_set_mouseenter_callback_on_thread(target, userData, useCapture, callbackfunc, targetThread) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(8, 0, 1, target, userData, useCapture, callbackfunc, targetThread);
  target = bigintToI53Checked(target);
  userData = bigintToI53Checked(userData);
  callbackfunc = bigintToI53Checked(callbackfunc);
  targetThread = bigintToI53Checked(targetThread);
  return registerMouseEventCallback(target, userData, useCapture, callbackfunc, 33, "mouseenter", targetThread);
}

function _emscripten_set_mousemove_callback_on_thread(target, userData, useCapture, callbackfunc, targetThread) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(9, 0, 1, target, userData, useCapture, callbackfunc, targetThread);
  target = bigintToI53Checked(target);
  userData = bigintToI53Checked(userData);
  callbackfunc = bigintToI53Checked(callbackfunc);
  targetThread = bigintToI53Checked(targetThread);
  return registerMouseEventCallback(target, userData, useCapture, callbackfunc, 8, "mousemove", targetThread);
}

function _emscripten_set_mouseup_callback_on_thread(target, userData, useCapture, callbackfunc, targetThread) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(10, 0, 1, target, userData, useCapture, callbackfunc, targetThread);
  target = bigintToI53Checked(target);
  userData = bigintToI53Checked(userData);
  callbackfunc = bigintToI53Checked(callbackfunc);
  targetThread = bigintToI53Checked(targetThread);
  return registerMouseEventCallback(target, userData, useCapture, callbackfunc, 6, "mouseup", targetThread);
}

var fillPointerlockChangeEventData = eventStruct => {
  var pointerLockElement = document.pointerLockElement;
  var isPointerlocked = !!pointerLockElement;
  // Assigning a boolean to HEAP32 with expected type coercion.
  /** @suppress{checkTypes} */ (growMemViews(), HEAP8)[eventStruct] = isPointerlocked;
  var nodeName = JSEvents.getNodeNameForTarget(pointerLockElement);
  var id = pointerLockElement?.id ?? "";
  stringToUTF8(nodeName, eventStruct + 1, 128);
  stringToUTF8(id, eventStruct + 129, 128);
};

var registerPointerlockChangeEventCallback = (target, userData, useCapture, callbackfunc, eventTypeId, eventTypeString, targetThread) => {
  targetThread = JSEvents.getTargetThreadForEventCallback(targetThread);
  var eventSize = 257;
  JSEvents.pointerlockChangeEvent ||= _malloc(eventSize);
  var pointerlockChangeEventHandlerFunc = e => {
    var pointerlockChangeEvent = JSEvents.pointerlockChangeEvent;
    fillPointerlockChangeEventData(pointerlockChangeEvent);
    if (targetThread) __emscripten_run_callback_on_thread(targetThread, callbackfunc, eventTypeId, pointerlockChangeEvent, eventSize, userData); else if (((a1, a2, a3) => getWasmTableEntry(callbackfunc).call(null, a1, BigInt(a2), BigInt(a3)))(eventTypeId, pointerlockChangeEvent, userData)) e.preventDefault();
  };
  var eventHandler = {
    target,
    eventTypeString,
    eventTypeId,
    userData,
    callbackfunc,
    handlerFunc: pointerlockChangeEventHandlerFunc,
    useCapture
  };
  return JSEvents.registerOrRemoveHandler(eventHandler);
};

function _emscripten_set_pointerlockchange_callback_on_thread(target, userData, useCapture, callbackfunc, targetThread) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(11, 0, 1, target, userData, useCapture, callbackfunc, targetThread);
  target = bigintToI53Checked(target);
  userData = bigintToI53Checked(userData);
  callbackfunc = bigintToI53Checked(callbackfunc);
  targetThread = bigintToI53Checked(targetThread);
  if (!document.body?.requestPointerLock) {
    return -1;
  }
  target = findEventTarget(target);
  if (!target) return -4;
  return registerPointerlockChangeEventCallback(target, userData, useCapture, callbackfunc, 20, "pointerlockchange", targetThread);
}

var registerUiEventCallback = (target, userData, useCapture, callbackfunc, eventTypeId, eventTypeString, targetThread) => {
  targetThread = JSEvents.getTargetThreadForEventCallback(targetThread);
  var eventSize = 36;
  JSEvents.uiEvent ||= _malloc(eventSize);
  target = findEventTarget(target);
  var uiEventHandlerFunc = e => {
    if (e.target != target) {
      // Never take ui events such as scroll via a 'bubbled' route, but always from the direct element that
      // was targeted. Otherwise e.g. if app logs a message in response to a page scroll, the Emscripten log
      // message box could cause to scroll, generating a new (bubbled) scroll message, causing a new log print,
      // causing a new scroll, etc..
      return;
    }
    var b = document.body;
    // Take document.body to a variable, Closure compiler does not outline access to it on its own.
    if (!b) {
      // During a page unload 'body' can be null, with "Cannot read property 'clientWidth' of null" being thrown
      return;
    }
    var uiEvent = JSEvents.uiEvent;
    (growMemViews(), HEAP32)[((uiEvent) >> 2)] = 0;
    // always zero for resize and scroll
    (growMemViews(), HEAP32)[(((uiEvent) + (4)) >> 2)] = b.clientWidth;
    (growMemViews(), HEAP32)[(((uiEvent) + (8)) >> 2)] = b.clientHeight;
    (growMemViews(), HEAP32)[(((uiEvent) + (12)) >> 2)] = innerWidth;
    (growMemViews(), HEAP32)[(((uiEvent) + (16)) >> 2)] = innerHeight;
    (growMemViews(), HEAP32)[(((uiEvent) + (20)) >> 2)] = outerWidth;
    (growMemViews(), HEAP32)[(((uiEvent) + (24)) >> 2)] = outerHeight;
    (growMemViews(), HEAP32)[(((uiEvent) + (28)) >> 2)] = pageXOffset | 0;
    // scroll offsets are float
    (growMemViews(), HEAP32)[(((uiEvent) + (32)) >> 2)] = pageYOffset | 0;
    if (targetThread) __emscripten_run_callback_on_thread(targetThread, callbackfunc, eventTypeId, uiEvent, eventSize, userData); else if (((a1, a2, a3) => getWasmTableEntry(callbackfunc).call(null, a1, BigInt(a2), BigInt(a3)))(eventTypeId, uiEvent, userData)) e.preventDefault();
  };
  var eventHandler = {
    target,
    eventTypeString,
    eventTypeId,
    userData,
    callbackfunc,
    handlerFunc: uiEventHandlerFunc,
    useCapture
  };
  return JSEvents.registerOrRemoveHandler(eventHandler);
};

function _emscripten_set_resize_callback_on_thread(target, userData, useCapture, callbackfunc, targetThread) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(12, 0, 1, target, userData, useCapture, callbackfunc, targetThread);
  target = bigintToI53Checked(target);
  userData = bigintToI53Checked(userData);
  callbackfunc = bigintToI53Checked(callbackfunc);
  targetThread = bigintToI53Checked(targetThread);
  return registerUiEventCallback(target, userData, useCapture, callbackfunc, 10, "resize", targetThread);
}

/** @param {number=} timeout */ var safeSetTimeout = (func, timeout) => {
  runtimeKeepalivePush();
  // Slot 0 is reserved so that, like setTimeout, ids are always non-zero.
  safeSetTimeout.mapping ||= [ 0 ];
  var id = safeSetTimeout.mapping.length;
  safeSetTimeout.mapping[id] = setTimeout(() => {
    safeSetTimeout.mapping[id] = undefined;
    runtimeKeepalivePop();
    callUserCallback(func);
  }, timeout);
  return id;
};

var _emscripten_set_timeout = function(cb, msecs, userData) {
  cb = bigintToI53Checked(cb);
  userData = bigintToI53Checked(userData);
  return safeSetTimeout(() => (a1 => getWasmTableEntry(cb).call(null, BigInt(a1)))(userData), msecs);
};

var registerTouchEventCallback = (target, userData, useCapture, callbackfunc, eventTypeId, eventTypeString, targetThread) => {
  targetThread = JSEvents.getTargetThreadForEventCallback(targetThread);
  var eventSize = 1552;
  JSEvents.touchEvent ||= _malloc(eventSize);
  target = findEventTarget(target);
  var touchEventHandlerFunc = e => {
    assert(e);
    var t, touches = {}, et = e.touches;
    // To ease marshalling different kinds of touches that browser reports (all touches are listed in e.touches,
    // only changed touches in e.changedTouches, and touches on target at a.targetTouches), mark a boolean in
    // each Touch object so that we can later loop only once over all touches we see to marshall over to Wasm.
    for (let t of et) {
      // Browser might recycle the generated Touch objects between each frame (Firefox on Android), so reset any
      // changed/target states we may have set from previous frame.
      t.isChanged = t.onTarget = 0;
      touches[t.identifier] = t;
    }
    // Mark which touches are part of the changedTouches list.
    for (let t of e.changedTouches) {
      t.isChanged = 1;
      touches[t.identifier] = t;
    }
    // Mark which touches are part of the targetTouches list.
    for (let t of e.targetTouches) {
      touches[t.identifier].onTarget = 1;
    }
    var touchEvent = JSEvents.touchEvent;
    (growMemViews(), HEAPF64)[((touchEvent) >> 3)] = e.timeStamp;
    (growMemViews(), HEAP8)[touchEvent + 12] = e.ctrlKey;
    (growMemViews(), HEAP8)[touchEvent + 13] = e.shiftKey;
    (growMemViews(), HEAP8)[touchEvent + 14] = e.altKey;
    (growMemViews(), HEAP8)[touchEvent + 15] = e.metaKey;
    var idx = touchEvent + 16;
    var targetRect = getBoundingClientRect(target);
    var numTouches = 0;
    for (let t of Object.values(touches)) {
      var idx32 = ((idx) >> 2);
      // Pre-shift the ptr to index to HEAP32 to save code size
      (growMemViews(), HEAP32)[idx32 + 0] = t.identifier;
      (growMemViews(), HEAP32)[idx32 + 1] = t.screenX;
      (growMemViews(), HEAP32)[idx32 + 2] = t.screenY;
      (growMemViews(), HEAP32)[idx32 + 3] = t.clientX;
      (growMemViews(), HEAP32)[idx32 + 4] = t.clientY;
      (growMemViews(), HEAP32)[idx32 + 5] = t.pageX;
      (growMemViews(), HEAP32)[idx32 + 6] = t.pageY;
      (growMemViews(), HEAP8)[idx + 28] = t.isChanged;
      (growMemViews(), HEAP8)[idx + 29] = t.onTarget;
      (growMemViews(), HEAP32)[idx32 + 8] = t.clientX - (targetRect.left | 0);
      (growMemViews(), HEAP32)[idx32 + 9] = t.clientY - (targetRect.top | 0);
      idx += 48;
      if (++numTouches > 31) {
        break;
      }
    }
    (growMemViews(), HEAP32)[(((touchEvent) + (8)) >> 2)] = numTouches;
    if (targetThread) __emscripten_run_callback_on_thread(targetThread, callbackfunc, eventTypeId, touchEvent, eventSize, userData); else if (((a1, a2, a3) => getWasmTableEntry(callbackfunc).call(null, a1, BigInt(a2), BigInt(a3)))(eventTypeId, touchEvent, userData)) e.preventDefault();
  };
  var eventHandler = {
    target,
    allowsDeferredCalls: eventTypeString == "touchstart" || eventTypeString == "touchend",
    eventTypeString,
    eventTypeId,
    userData,
    callbackfunc,
    handlerFunc: touchEventHandlerFunc,
    useCapture
  };
  return JSEvents.registerOrRemoveHandler(eventHandler);
};

function _emscripten_set_touchcancel_callback_on_thread(target, userData, useCapture, callbackfunc, targetThread) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(13, 0, 1, target, userData, useCapture, callbackfunc, targetThread);
  target = bigintToI53Checked(target);
  userData = bigintToI53Checked(userData);
  callbackfunc = bigintToI53Checked(callbackfunc);
  targetThread = bigintToI53Checked(targetThread);
  return registerTouchEventCallback(target, userData, useCapture, callbackfunc, 25, "touchcancel", targetThread);
}

function _emscripten_set_touchend_callback_on_thread(target, userData, useCapture, callbackfunc, targetThread) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(14, 0, 1, target, userData, useCapture, callbackfunc, targetThread);
  target = bigintToI53Checked(target);
  userData = bigintToI53Checked(userData);
  callbackfunc = bigintToI53Checked(callbackfunc);
  targetThread = bigintToI53Checked(targetThread);
  return registerTouchEventCallback(target, userData, useCapture, callbackfunc, 23, "touchend", targetThread);
}

function _emscripten_set_touchmove_callback_on_thread(target, userData, useCapture, callbackfunc, targetThread) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(15, 0, 1, target, userData, useCapture, callbackfunc, targetThread);
  target = bigintToI53Checked(target);
  userData = bigintToI53Checked(userData);
  callbackfunc = bigintToI53Checked(callbackfunc);
  targetThread = bigintToI53Checked(targetThread);
  return registerTouchEventCallback(target, userData, useCapture, callbackfunc, 24, "touchmove", targetThread);
}

function _emscripten_set_touchstart_callback_on_thread(target, userData, useCapture, callbackfunc, targetThread) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(16, 0, 1, target, userData, useCapture, callbackfunc, targetThread);
  target = bigintToI53Checked(target);
  userData = bigintToI53Checked(userData);
  callbackfunc = bigintToI53Checked(callbackfunc);
  targetThread = bigintToI53Checked(targetThread);
  return registerTouchEventCallback(target, userData, useCapture, callbackfunc, 22, "touchstart", targetThread);
}

var registerVisibilityChangeEventCallback = (target, userData, useCapture, callbackfunc, eventTypeId, eventTypeString, targetThread) => {
  targetThread = JSEvents.getTargetThreadForEventCallback(targetThread);
  var eventSize = 8;
  JSEvents.visibilityChangeEvent ||= _malloc(eventSize);
  var visibilityChangeEventHandlerFunc = e => {
    var visibilityChangeEvent = JSEvents.visibilityChangeEvent;
    fillVisibilityChangeEventData(visibilityChangeEvent);
    if (targetThread) __emscripten_run_callback_on_thread(targetThread, callbackfunc, eventTypeId, visibilityChangeEvent, eventSize, userData); else if (((a1, a2, a3) => getWasmTableEntry(callbackfunc).call(null, a1, BigInt(a2), BigInt(a3)))(eventTypeId, visibilityChangeEvent, userData)) e.preventDefault();
  };
  var eventHandler = {
    target,
    eventTypeString,
    eventTypeId,
    userData,
    callbackfunc,
    handlerFunc: visibilityChangeEventHandlerFunc,
    useCapture
  };
  return JSEvents.registerOrRemoveHandler(eventHandler);
};

function _emscripten_set_visibilitychange_callback_on_thread(userData, useCapture, callbackfunc, targetThread) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(17, 0, 1, userData, useCapture, callbackfunc, targetThread);
  userData = bigintToI53Checked(userData);
  callbackfunc = bigintToI53Checked(callbackfunc);
  targetThread = bigintToI53Checked(targetThread);
  if (!specialHTMLTargets[1]) {
    return -4;
  }
  return registerVisibilityChangeEventCallback(specialHTMLTargets[1], userData, useCapture, callbackfunc, 21, "visibilitychange", targetThread);
}

var registerWheelEventCallback = (target, userData, useCapture, callbackfunc, eventTypeId, eventTypeString, targetThread) => {
  targetThread = JSEvents.getTargetThreadForEventCallback(targetThread);
  var eventSize = 96;
  JSEvents.wheelEvent ||= _malloc(eventSize);
  // The DOM Level 3 events spec event 'wheel'
  var wheelHandlerFunc = e => {
    var wheelEvent = JSEvents.wheelEvent;
    fillMouseEventData(wheelEvent, e, target);
    (growMemViews(), HEAPF64)[(((wheelEvent) + (64)) >> 3)] = e["deltaX"];
    (growMemViews(), HEAPF64)[(((wheelEvent) + (72)) >> 3)] = e["deltaY"];
    (growMemViews(), HEAPF64)[(((wheelEvent) + (80)) >> 3)] = e["deltaZ"];
    (growMemViews(), HEAP32)[(((wheelEvent) + (88)) >> 2)] = e["deltaMode"];
    if (targetThread) __emscripten_run_callback_on_thread(targetThread, callbackfunc, eventTypeId, wheelEvent, eventSize, userData); else if (((a1, a2, a3) => getWasmTableEntry(callbackfunc).call(null, a1, BigInt(a2), BigInt(a3)))(eventTypeId, wheelEvent, userData)) e.preventDefault();
  };
  var eventHandler = {
    target,
    allowsDeferredCalls: true,
    eventTypeString,
    eventTypeId,
    userData,
    callbackfunc,
    handlerFunc: wheelHandlerFunc,
    useCapture
  };
  return JSEvents.registerOrRemoveHandler(eventHandler);
};

function _emscripten_set_wheel_callback_on_thread(target, userData, useCapture, callbackfunc, targetThread) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(18, 0, 1, target, userData, useCapture, callbackfunc, targetThread);
  target = bigintToI53Checked(target);
  userData = bigintToI53Checked(userData);
  callbackfunc = bigintToI53Checked(callbackfunc);
  targetThread = bigintToI53Checked(targetThread);
  target = findEventTarget(target);
  if (!target) return -4;
  if (typeof target.onwheel != "undefined") {
    return registerWheelEventCallback(target, userData, useCapture, callbackfunc, 9, "wheel", targetThread);
  } else {
    return -1;
  }
}

var _emscripten_unwind_to_js_event_loop = () => {
  throw "unwind";
};

var stringToUTF8OnStack = str => {
  var size = lengthBytesUTF8(str) + 1;
  var ret = stackAlloc(size);
  stringToUTF8(str, ret, size);
  return ret;
};

var readI53FromI64 = ptr => (growMemViews(), HEAPU32)[((ptr) >> 2)] + (growMemViews(), 
HEAP32)[(((ptr) + (4)) >> 2)] * 4294967296;

var readI53FromU64 = ptr => (growMemViews(), HEAPU32)[((ptr) >> 2)] + (growMemViews(), 
HEAPU32)[(((ptr) + (4)) >> 2)] * 4294967296;

var writeI53ToI64 = (ptr, num) => {
  (growMemViews(), HEAPU32)[((ptr) >> 2)] = num;
  var lower = (growMemViews(), HEAPU32)[((ptr) >> 2)];
  (growMemViews(), HEAPU32)[(((ptr) + (4)) >> 2)] = (num - lower) / 4294967296;
  var deserialized = (num >= 0) ? readI53FromU64(ptr) : readI53FromI64(ptr);
  var offset = ((ptr) >> 2);
  if (deserialized != num) warnOnce(`writeI53ToI64() out of range: serialized JS Number ${num} to Wasm heap as bytes lo=${ptrToString((growMemViews(), 
  HEAPU32)[offset])}, hi=${ptrToString((growMemViews(), HEAPU32)[offset + 1])}, which deserializes back to ${deserialized} instead!`);
};

/** @type {!Float32Array} */ var HEAPF32;

var WebGPU = {
  Internals: {
    jsObjects: [],
    jsObjectInsert: (ptr, jsObject) => {
      WebGPU.Internals.jsObjects[ptr] = jsObject;
    },
    bufferOnUnmaps: [],
    futures: [],
    futureInsert: (futureId, promise) => {}
  },
  getJsObject: ptr => {
    if (!ptr) return undefined;
    assert(ptr in WebGPU.Internals.jsObjects);
    return WebGPU.Internals.jsObjects[ptr];
  },
  importJsAdapter: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateAdapter(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsBindGroup: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateBindGroup(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsBindGroupLayout: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateBindGroupLayout(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsBuffer: (buffer, parentPtr = 0) => {
    // At the moment, we do not allow importing pending buffers.
    assert(buffer.mapState === "unmapped");
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64)
    var bufferPtr = _emwgpuImportBuffer(BigInt(parentPtr));
    WebGPU.Internals.jsObjectInsert(bufferPtr, buffer);
    return bufferPtr;
  },
  importJsCommandBuffer: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateCommandBuffer(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsCommandEncoder: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateCommandEncoder(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsComputePassEncoder: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateComputePassEncoder(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsComputePipeline: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateComputePipeline(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsDevice: (device, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws, so a
    // page-made device given in Module.preinitializedWebGPUDevice could not be imported)
    var queuePtr = _emwgpuCreateQueue(BigInt(parentPtr));
    var devicePtr = _emwgpuCreateDevice(BigInt(parentPtr), BigInt(queuePtr));
    WebGPU.Internals.jsObjectInsert(queuePtr, device.queue);
    WebGPU.Internals.jsObjectInsert(devicePtr, device);
    return devicePtr;
  },
  importJsExternalTexture: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateExternalTexture(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsPipelineLayout: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreatePipelineLayout(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsQuerySet: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateQuerySet(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsQueue: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateQueue(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsRenderBundle: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateRenderBundle(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsRenderBundleEncoder: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateRenderBundleEncoder(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsRenderPassEncoder: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateRenderPassEncoder(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsRenderPipeline: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateRenderPipeline(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsSampler: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateSampler(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsShaderModule: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateShaderModule(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsSurface: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateSurface(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsTexture: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateTexture(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  importJsTextureView: (obj, parentPtr = 0) => {
    // UE 5.9 change: the parent pointer as a pointer argument (a BigInt on wasm64, where a plain number throws)
    var ptr = _emwgpuCreateTextureView(BigInt(parentPtr));
    WebGPU.Internals.jsObjects[ptr] = obj;
    return ptr;
  },
  errorCallback: (callback, type, message, userdata) => {
    var sp = stackSave();
    var messagePtr = stringToUTF8OnStack(message);
    ((a1, a2, a3) => getWasmTableEntry(callback).call(null, a1, BigInt(a2), BigInt(a3)))(type, BigInt(messagePtr), userdata);
    stackRestore(sp);
  },
  iterateExtensions: (root, handlers) => {
    assert(root);
    for (var ptr = Number((growMemViews(), HEAPU64)[((root) >> 3)]); ptr; ptr = Number((growMemViews(), 
    HEAPU64)[((ptr) >> 3)])) {
      var sType = (growMemViews(), HEAP32)[(((ptr) + (8)) >> 2)];
      // This will crash if there's no handler indicating either a bogus
      // sType, or one we haven't implemented yet.
      var handler = handlers[sType](ptr);
    }
  },
  setStringView: (ptr, data, length) => {
    (growMemViews(), HEAPU64)[((ptr) >> 3)] = BigInt(data);
    (growMemViews(), HEAPU64)[(((ptr) + (8)) >> 3)] = BigInt(length);
  },
  makeStringFromStringView: stringViewPtr => {
    var ptr = Number((growMemViews(), HEAPU64)[((stringViewPtr) >> 3)]);
    var length = Number((growMemViews(), HEAPU64)[(((stringViewPtr) + (8)) >> 3)]);
    // UTF8ToString stops at the first null terminator character in the
    // string regardless of the length.
    return UTF8ToString(ptr, length);
  },
  makeStringFromOptionalStringView: stringViewPtr => {
    var ptr = Number((growMemViews(), HEAPU64)[((stringViewPtr) >> 3)]);
    var length = Number((growMemViews(), HEAPU64)[(((stringViewPtr) + (8)) >> 3)]);
    // If we don't have a valid string pointer, just return undefined when
    // optional.
    if (!ptr) {
      if (length === 0) {
        return "";
      }
      return undefined;
    }
    // UTF8ToString stops at the first null terminator character in the
    // string regardless of the length.
    return UTF8ToString(ptr, length);
  },
  makeColor: ptr => ({
    "r": (growMemViews(), HEAPF64)[((ptr) >> 3)],
    "g": (growMemViews(), HEAPF64)[(((ptr) + (8)) >> 3)],
    "b": (growMemViews(), HEAPF64)[(((ptr) + (16)) >> 3)],
    "a": (growMemViews(), HEAPF64)[(((ptr) + (24)) >> 3)]
  }),
  makeExtent3D: ptr => ({
    "width": (growMemViews(), HEAPU32)[((ptr) >> 2)],
    "height": (growMemViews(), HEAPU32)[(((ptr) + (4)) >> 2)],
    "depthOrArrayLayers": (growMemViews(), HEAPU32)[(((ptr) + (8)) >> 2)]
  }),
  makeOrigin3D: ptr => ({
    "x": (growMemViews(), HEAPU32)[((ptr) >> 2)],
    "y": (growMemViews(), HEAPU32)[(((ptr) + (4)) >> 2)],
    "z": (growMemViews(), HEAPU32)[(((ptr) + (8)) >> 2)]
  }),
  makeTexelCopyTextureInfo: ptr => {
    assert(ptr);
    return {
      "texture": WebGPU.getJsObject(Number((growMemViews(), HEAPU64)[((ptr) >> 3)])),
      "mipLevel": (growMemViews(), HEAPU32)[(((ptr) + (8)) >> 2)],
      "origin": WebGPU.makeOrigin3D(ptr + 12),
      "aspect": WebGPU.TextureAspect[(growMemViews(), HEAP32)[(((ptr) + (24)) >> 2)]]
    };
  },
  makeTexelCopyBufferLayout: ptr => {
    var bytesPerRow = (growMemViews(), HEAPU32)[(((ptr) + (8)) >> 2)];
    var rowsPerImage = (growMemViews(), HEAPU32)[(((ptr) + (12)) >> 2)];
    return {
      "offset": readI53FromI64(ptr),
      "bytesPerRow": bytesPerRow === 4294967295 ? undefined : bytesPerRow,
      "rowsPerImage": rowsPerImage === 4294967295 ? undefined : rowsPerImage
    };
  },
  makeTexelCopyBufferInfo: ptr => {
    assert(ptr);
    var layoutPtr = ptr + 0;
    var bufferCopyView = WebGPU.makeTexelCopyBufferLayout(layoutPtr);
    bufferCopyView["buffer"] = WebGPU.getJsObject(Number((growMemViews(), HEAPU64)[(((ptr) + (16)) >> 3)]));
    return bufferCopyView;
  },
  makePassTimestampWrites: ptr => {
    if (ptr === 0) return undefined;
    return {
      "querySet": WebGPU.getJsObject(Number((growMemViews(), HEAPU64)[(((ptr) + (8)) >> 3)])),
      "beginningOfPassWriteIndex": (growMemViews(), HEAPU32)[(((ptr) + (16)) >> 2)],
      "endOfPassWriteIndex": (growMemViews(), HEAPU32)[(((ptr) + (20)) >> 2)]
    };
  },
  makePipelineConstants: (constantCount, constantsPtr) => {
    if (!constantCount) return;
    var constants = {};
    for (var i = 0; i < constantCount; ++i) {
      var entryPtr = constantsPtr + 32 * i;
      var key = WebGPU.makeStringFromStringView(entryPtr + 8);
      constants[key] = (growMemViews(), HEAPF64)[(((entryPtr) + (24)) >> 3)];
    }
    return constants;
  },
  makePipelineLayout: layoutPtr => {
    if (!layoutPtr) return "auto";
    return WebGPU.getJsObject(layoutPtr);
  },
  makeComputeState: ptr => {
    if (!ptr) return undefined;
    assert(ptr);
    assert(Number((growMemViews(), HEAPU64)[((ptr) >> 3)]) === 0);
    var desc = {
      "module": WebGPU.getJsObject(Number((growMemViews(), HEAPU64)[(((ptr) + (8)) >> 3)])),
      "constants": WebGPU.makePipelineConstants(Number((growMemViews(), HEAPU64)[(((ptr) + (32)) >> 3)]), Number((growMemViews(), 
      HEAPU64)[(((ptr) + (40)) >> 3)])),
      "entryPoint": WebGPU.makeStringFromOptionalStringView(ptr + 16)
    };
    return desc;
  },
  makeComputePipelineDesc: descriptor => {
    assert(descriptor);
    assert(Number((growMemViews(), HEAPU64)[((descriptor) >> 3)]) === 0);
    var desc = {
      "label": WebGPU.makeStringFromOptionalStringView(descriptor + 8),
      "layout": WebGPU.makePipelineLayout(Number((growMemViews(), HEAPU64)[(((descriptor) + (24)) >> 3)])),
      "compute": WebGPU.makeComputeState(descriptor + 32)
    };
    return desc;
  },
  makeRenderPipelineDesc: descriptor => {
    assert(descriptor);
    assert(Number((growMemViews(), HEAPU64)[((descriptor) >> 3)]) === 0);
    function makePrimitiveState(psPtr) {
      if (!psPtr) return undefined;
      assert(psPtr);
      assert(Number((growMemViews(), HEAPU64)[((psPtr) >> 3)]) === 0);
      return {
        "topology": WebGPU.PrimitiveTopology[(growMemViews(), HEAP32)[(((psPtr) + (8)) >> 2)]],
        "stripIndexFormat": WebGPU.IndexFormat[(growMemViews(), HEAP32)[(((psPtr) + (12)) >> 2)]],
        "frontFace": WebGPU.FrontFace[(growMemViews(), HEAP32)[(((psPtr) + (16)) >> 2)]],
        "cullMode": WebGPU.CullMode[(growMemViews(), HEAP32)[(((psPtr) + (20)) >> 2)]],
        "unclippedDepth": !!((growMemViews(), HEAPU32)[(((psPtr) + (24)) >> 2)])
      };
    }
    function makeBlendComponent(bdPtr) {
      if (!bdPtr) return undefined;
      return {
        "operation": WebGPU.BlendOperation[(growMemViews(), HEAP32)[((bdPtr) >> 2)]],
        "srcFactor": WebGPU.BlendFactor[(growMemViews(), HEAP32)[(((bdPtr) + (4)) >> 2)]],
        "dstFactor": WebGPU.BlendFactor[(growMemViews(), HEAP32)[(((bdPtr) + (8)) >> 2)]]
      };
    }
    function makeBlendState(bsPtr) {
      if (!bsPtr) return undefined;
      return {
        "alpha": makeBlendComponent(bsPtr + 12),
        "color": makeBlendComponent(bsPtr + 0)
      };
    }
    function makeColorState(csPtr) {
      assert(csPtr);
      assert(Number((growMemViews(), HEAPU64)[((csPtr) >> 3)]) === 0);
      var format = WebGPU.TextureFormat[(growMemViews(), HEAP32)[(((csPtr) + (8)) >> 2)]];
      return format ? {
        "format": format,
        "blend": makeBlendState(Number((growMemViews(), HEAPU64)[(((csPtr) + (16)) >> 3)])),
        "writeMask": (growMemViews(), HEAPU32)[(((csPtr) + (24)) >> 2)]
      } : undefined;
    }
    function makeColorStates(count, csArrayPtr) {
      var states = [];
      for (var i = 0; i < count; ++i) {
        states.push(makeColorState(csArrayPtr + 32 * i));
      }
      return states;
    }
    function makeStencilStateFace(ssfPtr) {
      assert(ssfPtr);
      return {
        "compare": WebGPU.CompareFunction[(growMemViews(), HEAP32)[((ssfPtr) >> 2)]],
        "failOp": WebGPU.StencilOperation[(growMemViews(), HEAP32)[(((ssfPtr) + (4)) >> 2)]],
        "depthFailOp": WebGPU.StencilOperation[(growMemViews(), HEAP32)[(((ssfPtr) + (8)) >> 2)]],
        "passOp": WebGPU.StencilOperation[(growMemViews(), HEAP32)[(((ssfPtr) + (12)) >> 2)]]
      };
    }
    function makeDepthStencilState(dssPtr) {
      if (!dssPtr) return undefined;
      assert(dssPtr);
      return {
        "format": WebGPU.TextureFormat[(growMemViews(), HEAP32)[(((dssPtr) + (8)) >> 2)]],
        "depthWriteEnabled": !!((growMemViews(), HEAPU32)[(((dssPtr) + (12)) >> 2)]),
        "depthCompare": WebGPU.CompareFunction[(growMemViews(), HEAP32)[(((dssPtr) + (16)) >> 2)]],
        "stencilFront": makeStencilStateFace(dssPtr + 20),
        "stencilBack": makeStencilStateFace(dssPtr + 36),
        "stencilReadMask": (growMemViews(), HEAPU32)[(((dssPtr) + (52)) >> 2)],
        "stencilWriteMask": (growMemViews(), HEAPU32)[(((dssPtr) + (56)) >> 2)],
        "depthBias": (growMemViews(), HEAP32)[(((dssPtr) + (60)) >> 2)],
        "depthBiasSlopeScale": (growMemViews(), HEAPF32)[(((dssPtr) + (64)) >> 2)],
        "depthBiasClamp": (growMemViews(), HEAPF32)[(((dssPtr) + (68)) >> 2)]
      };
    }
    function makeVertexAttribute(vaPtr) {
      assert(vaPtr);
      return {
        "format": WebGPU.VertexFormat[(growMemViews(), HEAP32)[(((vaPtr) + (8)) >> 2)]],
        "offset": readI53FromI64((vaPtr) + (16)),
        "shaderLocation": (growMemViews(), HEAPU32)[(((vaPtr) + (24)) >> 2)]
      };
    }
    function makeVertexAttributes(count, vaArrayPtr) {
      var vas = [];
      for (var i = 0; i < count; ++i) {
        vas.push(makeVertexAttribute(vaArrayPtr + i * 32));
      }
      return vas;
    }
    function makeVertexBuffer(vbPtr) {
      if (!vbPtr) return undefined;
      var stepMode = WebGPU.VertexStepMode[(growMemViews(), HEAP32)[(((vbPtr) + (8)) >> 2)]];
      var attributeCount = Number((growMemViews(), HEAPU64)[(((vbPtr) + (24)) >> 3)]);
      if (!stepMode && !attributeCount) {
        return null;
      }
      return {
        "arrayStride": readI53FromI64((vbPtr) + (16)),
        "stepMode": stepMode,
        "attributes": makeVertexAttributes(attributeCount, Number((growMemViews(), HEAPU64)[(((vbPtr) + (32)) >> 3)]))
      };
    }
    function makeVertexBuffers(count, vbArrayPtr) {
      if (!count) return undefined;
      var vbs = [];
      for (var i = 0; i < count; ++i) {
        vbs.push(makeVertexBuffer(vbArrayPtr + i * 40));
      }
      return vbs;
    }
    function makeVertexState(viPtr) {
      if (!viPtr) return undefined;
      assert(viPtr);
      assert(Number((growMemViews(), HEAPU64)[((viPtr) >> 3)]) === 0);
      var desc = {
        "module": WebGPU.getJsObject(Number((growMemViews(), HEAPU64)[(((viPtr) + (8)) >> 3)])),
        "constants": WebGPU.makePipelineConstants(Number((growMemViews(), HEAPU64)[(((viPtr) + (32)) >> 3)]), Number((growMemViews(), 
        HEAPU64)[(((viPtr) + (40)) >> 3)])),
        "buffers": makeVertexBuffers(Number((growMemViews(), HEAPU64)[(((viPtr) + (48)) >> 3)]), Number((growMemViews(), 
        HEAPU64)[(((viPtr) + (56)) >> 3)])),
        "entryPoint": WebGPU.makeStringFromOptionalStringView(viPtr + 16)
      };
      return desc;
    }
    function makeMultisampleState(msPtr) {
      if (!msPtr) return undefined;
      assert(msPtr);
      assert(Number((growMemViews(), HEAPU64)[((msPtr) >> 3)]) === 0);
      return {
        "count": (growMemViews(), HEAPU32)[(((msPtr) + (8)) >> 2)],
        "mask": (growMemViews(), HEAPU32)[(((msPtr) + (12)) >> 2)],
        "alphaToCoverageEnabled": !!((growMemViews(), HEAPU32)[(((msPtr) + (16)) >> 2)])
      };
    }
    function makeFragmentState(fsPtr) {
      if (!fsPtr) return undefined;
      assert(fsPtr);
      assert(Number((growMemViews(), HEAPU64)[((fsPtr) >> 3)]) === 0);
      var desc = {
        "module": WebGPU.getJsObject(Number((growMemViews(), HEAPU64)[(((fsPtr) + (8)) >> 3)])),
        "constants": WebGPU.makePipelineConstants(Number((growMemViews(), HEAPU64)[(((fsPtr) + (32)) >> 3)]), Number((growMemViews(), 
        HEAPU64)[(((fsPtr) + (40)) >> 3)])),
        "targets": makeColorStates(Number((growMemViews(), HEAPU64)[(((fsPtr) + (48)) >> 3)]), Number((growMemViews(), 
        HEAPU64)[(((fsPtr) + (56)) >> 3)])),
        "entryPoint": WebGPU.makeStringFromOptionalStringView(fsPtr + 16)
      };
      return desc;
    }
    var desc = {
      "label": WebGPU.makeStringFromOptionalStringView(descriptor + 8),
      "layout": WebGPU.makePipelineLayout(Number((growMemViews(), HEAPU64)[(((descriptor) + (24)) >> 3)])),
      "vertex": makeVertexState(descriptor + 32),
      "primitive": makePrimitiveState(descriptor + 96),
      "depthStencil": makeDepthStencilState(Number((growMemViews(), HEAPU64)[(((descriptor) + (128)) >> 3)])),
      "multisample": makeMultisampleState(descriptor + 136),
      "fragment": makeFragmentState(Number((growMemViews(), HEAPU64)[(((descriptor) + (160)) >> 3)]))
    };
    return desc;
  },
  fillLimitStruct: (limits, limitsOutPtr) => {
    assert(limitsOutPtr);
    var nextInChainPtr = Number((growMemViews(), HEAPU64)[((limitsOutPtr) >> 3)]);
    function setLimitValueU32(name, basePtr, limitOffset, fallbackValue = 0) {
      var limitValue = limits[name] ?? fallbackValue;
      (growMemViews(), HEAPU32)[(((basePtr) + (limitOffset)) >> 2)] = limitValue;
    }
    function setLimitValueU64(name, basePtr, limitOffset, fallbackValue = 0) {
      var limitValue = limits[name] ?? fallbackValue;
      // Limits are integer-valued JS `Number`s, so they fit in 'i53'.
      writeI53ToI64((basePtr) + (limitOffset), limitValue);
    }
    setLimitValueU32("maxTextureDimension1D", limitsOutPtr, 8);
    setLimitValueU32("maxTextureDimension2D", limitsOutPtr, 12);
    setLimitValueU32("maxTextureDimension3D", limitsOutPtr, 16);
    setLimitValueU32("maxTextureArrayLayers", limitsOutPtr, 20);
    setLimitValueU32("maxBindGroups", limitsOutPtr, 24);
    setLimitValueU32("maxBindGroupsPlusVertexBuffers", limitsOutPtr, 28);
    setLimitValueU32("maxBindingsPerBindGroup", limitsOutPtr, 32);
    setLimitValueU32("maxDynamicUniformBuffersPerPipelineLayout", limitsOutPtr, 36);
    setLimitValueU32("maxDynamicStorageBuffersPerPipelineLayout", limitsOutPtr, 40);
    setLimitValueU32("maxSampledTexturesPerShaderStage", limitsOutPtr, 44);
    setLimitValueU32("maxSamplersPerShaderStage", limitsOutPtr, 48);
    setLimitValueU32("maxStorageBuffersPerShaderStage", limitsOutPtr, 52);
    setLimitValueU32("maxStorageTexturesPerShaderStage", limitsOutPtr, 56);
    setLimitValueU32("maxUniformBuffersPerShaderStage", limitsOutPtr, 60);
    setLimitValueU32("minUniformBufferOffsetAlignment", limitsOutPtr, 80);
    setLimitValueU32("minStorageBufferOffsetAlignment", limitsOutPtr, 84);
    setLimitValueU64("maxUniformBufferBindingSize", limitsOutPtr, 64);
    setLimitValueU64("maxStorageBufferBindingSize", limitsOutPtr, 72);
    setLimitValueU32("maxVertexBuffers", limitsOutPtr, 88);
    setLimitValueU64("maxBufferSize", limitsOutPtr, 96);
    setLimitValueU32("maxVertexAttributes", limitsOutPtr, 104);
    setLimitValueU32("maxVertexBufferArrayStride", limitsOutPtr, 108);
    setLimitValueU32("maxInterStageShaderVariables", limitsOutPtr, 112);
    setLimitValueU32("maxColorAttachments", limitsOutPtr, 116);
    setLimitValueU32("maxColorAttachmentBytesPerSample", limitsOutPtr, 120);
    setLimitValueU32("maxComputeWorkgroupStorageSize", limitsOutPtr, 124);
    setLimitValueU32("maxComputeInvocationsPerWorkgroup", limitsOutPtr, 128);
    setLimitValueU32("maxComputeWorkgroupSizeX", limitsOutPtr, 132);
    setLimitValueU32("maxComputeWorkgroupSizeY", limitsOutPtr, 136);
    setLimitValueU32("maxComputeWorkgroupSizeZ", limitsOutPtr, 140);
    setLimitValueU32("maxComputeWorkgroupsPerDimension", limitsOutPtr, 144);
    // Note this limit is new and won't be present in all browsers for a while. Fall back to 0.
    setLimitValueU32("maxImmediateSize", limitsOutPtr, 148);
    if (nextInChainPtr !== 0) {
      var sType = (growMemViews(), HEAP32)[(((nextInChainPtr) + (8)) >> 2)];
      assert(sType === 15);
      assert(0 === Number((growMemViews(), HEAPU64)[((nextInChainPtr) >> 3)]));
      var compatibilityModeLimitsPtr = nextInChainPtr;
      assert(compatibilityModeLimitsPtr);
      assert(Number((growMemViews(), HEAPU64)[((compatibilityModeLimitsPtr) >> 3)]) === 0);
      // Note these limits are new and won't be present in all browsers for a while. Fall back to exposing the PerShaderStage limit.
      setLimitValueU32("maxStorageBuffersInVertexStage", compatibilityModeLimitsPtr, 16, limits.maxStorageBuffersPerShaderStage);
      setLimitValueU32("maxStorageBuffersInFragmentStage", compatibilityModeLimitsPtr, 24, limits.maxStorageBuffersPerShaderStage);
      setLimitValueU32("maxStorageTexturesInVertexStage", compatibilityModeLimitsPtr, 20, limits.maxStorageTexturesPerShaderStage);
      setLimitValueU32("maxStorageTexturesInFragmentStage", compatibilityModeLimitsPtr, 28, limits.maxStorageTexturesPerShaderStage);
    }
  },
  fillAdapterInfoStruct: (info, infoStruct) => {
    assert(infoStruct);
    // Populate subgroup limits.
    (growMemViews(), HEAPU32)[(((infoStruct) + (88)) >> 2)] = info.subgroupMinSize;
    (growMemViews(), HEAPU32)[(((infoStruct) + (92)) >> 2)] = info.subgroupMaxSize;
    // Append all the strings together to condense into a single malloc.
    var strs = info.vendor + info.architecture + info.device + info.description;
    var strPtr = stringToNewUTF8(strs);
    var vendorLen = lengthBytesUTF8(info.vendor);
    WebGPU.setStringView(infoStruct + 8, strPtr, vendorLen);
    strPtr += vendorLen;
    var architectureLen = lengthBytesUTF8(info.architecture);
    WebGPU.setStringView(infoStruct + 24, strPtr, architectureLen);
    strPtr += architectureLen;
    var deviceLen = lengthBytesUTF8(info.device);
    WebGPU.setStringView(infoStruct + 40, strPtr, deviceLen);
    strPtr += deviceLen;
    var descriptionLen = lengthBytesUTF8(info.description);
    WebGPU.setStringView(infoStruct + 56, strPtr, descriptionLen);
    strPtr += descriptionLen;
    (growMemViews(), HEAP32)[(((infoStruct) + (72)) >> 2)] = 2;
    var adapterType = info.isFallbackAdapter ? 3 : 4;
    (growMemViews(), HEAP32)[(((infoStruct) + (76)) >> 2)] = adapterType;
    (growMemViews(), HEAPU32)[(((infoStruct) + (80)) >> 2)] = 0;
    (growMemViews(), HEAPU32)[(((infoStruct) + (84)) >> 2)] = 0;
    WebGPU.iterateExtensions(infoStruct, {
      327739: ext => {
        let configCount = 0;
        let configsPtr = 0n;
        if (info.subgroupMatrixConfigs) {
          configCount = info.subgroupMatrixConfigs.length;
          configsPtr = _malloc(configCount * 20);
          for (const [i, cfg] of info.subgroupMatrixConfigs.entries()) {
            const ptr = configsPtr + i * 20;
            const componentType = WebGPU.SubgroupMatrixComponentType.indexOf(cfg.componentType);
            const resultComponentType = WebGPU.SubgroupMatrixComponentType.indexOf(cfg.resultComponentType);
            (growMemViews(), HEAP32)[((ptr) >> 2)] = componentType;
            (growMemViews(), HEAP32)[(((ptr) + (4)) >> 2)] = resultComponentType;
            (growMemViews(), HEAPU32)[(((ptr) + (8)) >> 2)] = cfg.M;
            (growMemViews(), HEAPU32)[(((ptr) + (12)) >> 2)] = cfg.N;
            (growMemViews(), HEAPU32)[(((ptr) + (16)) >> 2)] = cfg.K;
          }
        }
        (growMemViews(), HEAPU64)[(((ext) + (24)) >> 3)] = BigInt(configsPtr);
        (growMemViews(), HEAPU64)[(((ext) + (16)) >> 3)] = BigInt(configCount);
      }
    });
  },
  AddressMode: [ , "clamp-to-edge", "repeat", "mirror-repeat" ],
  BlendFactor: [ , "zero", "one", "src", "one-minus-src", "src-alpha", "one-minus-src-alpha", "dst", "one-minus-dst", "dst-alpha", "one-minus-dst-alpha", "src-alpha-saturated", "constant", "one-minus-constant", "src1", "one-minus-src1", "src1-alpha", "one-minus-src1-alpha" ],
  BlendOperation: [ , "add", "subtract", "reverse-subtract", "min", "max" ],
  BufferBindingType: [ , , "uniform", "storage", "read-only-storage" ],
  BufferMapState: [ , "unmapped", "pending", "mapped" ],
  CompareFunction: [ , "never", "less", "equal", "less-equal", "greater", "not-equal", "greater-equal", "always" ],
  CompilationInfoRequestStatus: [ , "success", "callback-cancelled" ],
  ComponentSwizzle: [ , "0", "1", "r", "g", "b", "a" ],
  CompositeAlphaMode: [ , "opaque", "premultiplied", "unpremultiplied", "inherit" ],
  CullMode: [ , "none", "front", "back" ],
  ErrorFilter: [ , "validation", "out-of-memory", "internal" ],
  FeatureLevel: [ , "compatibility", "core" ],
  FeatureName: {
    1: "core-features-and-limits",
    2: "depth-clip-control",
    3: "depth32float-stencil8",
    4: "texture-compression-bc",
    5: "texture-compression-bc-sliced-3d",
    6: "texture-compression-etc2",
    7: "texture-compression-astc",
    8: "texture-compression-astc-sliced-3d",
    9: "timestamp-query",
    10: "indirect-first-instance",
    11: "shader-f16",
    12: "rg11b10ufloat-renderable",
    13: "bgra8unorm-storage",
    14: "float32-filterable",
    15: "float32-blendable",
    16: "clip-distances",
    17: "dual-source-blending",
    18: "subgroups",
    19: "texture-formats-tier1",
    20: "texture-formats-tier2",
    21: "primitive-index",
    22: "texture-component-swizzle",
    23: "subgroup-size-control",
    327692: "chromium-experimental-unorm16-texture-formats",
    327729: "chromium-experimental-multi-draw-indirect",
    327732: "chromium-experimental-subgroup-matrix"
  },
  FilterMode: [ , "nearest", "linear" ],
  FrontFace: [ , "ccw", "cw" ],
  IndexFormat: [ , "uint16", "uint32" ],
  InstanceFeatureName: [ , "timed-wait-any", "shader-source-spirv", "multiple-devices-per-adapter" ],
  LoadOp: [ , "load", "clear" ],
  MipmapFilterMode: [ , "nearest", "linear" ],
  OptionalBool: [ "false", "true" ],
  PowerPreference: [ , "low-power", "high-performance" ],
  PredefinedColorSpace: [ , "srgb", "display-p3" ],
  PrimitiveTopology: [ , "point-list", "line-list", "line-strip", "triangle-list", "triangle-strip" ],
  QueryType: [ , "occlusion", "timestamp" ],
  SamplerBindingType: [ , , "filtering", "non-filtering", "comparison" ],
  Status: [ , "success", "error" ],
  StencilOperation: [ , "keep", "zero", "replace", "invert", "increment-clamp", "decrement-clamp", "increment-wrap", "decrement-wrap" ],
  StorageTextureAccess: [ , , "write-only", "read-only", "read-write" ],
  StoreOp: [ , "store", "discard" ],
  SubgroupMatrixComponentType: [ , "f32", "f16", "u32", "i32", "u8", "i8" ],
  SurfaceGetCurrentTextureStatus: [ , "success-optimal", "success-suboptimal", "timeout", "outdated", "lost", "error" ],
  TextureAspect: [ , "all", "stencil-only", "depth-only" ],
  TextureDimension: [ , "1d", "2d", "3d" ],
  TextureFormat: [ , "r8unorm", "r8snorm", "r8uint", "r8sint", "r16unorm", "r16snorm", "r16uint", "r16sint", "r16float", "rg8unorm", "rg8snorm", "rg8uint", "rg8sint", "r32float", "r32uint", "r32sint", "rg16unorm", "rg16snorm", "rg16uint", "rg16sint", "rg16float", "rgba8unorm", "rgba8unorm-srgb", "rgba8snorm", "rgba8uint", "rgba8sint", "bgra8unorm", "bgra8unorm-srgb", "rgb10a2uint", "rgb10a2unorm", "rg11b10ufloat", "rgb9e5ufloat", "rg32float", "rg32uint", "rg32sint", "rgba16unorm", "rgba16snorm", "rgba16uint", "rgba16sint", "rgba16float", "rgba32float", "rgba32uint", "rgba32sint", "stencil8", "depth16unorm", "depth24plus", "depth24plus-stencil8", "depth32float", "depth32float-stencil8", "bc1-rgba-unorm", "bc1-rgba-unorm-srgb", "bc2-rgba-unorm", "bc2-rgba-unorm-srgb", "bc3-rgba-unorm", "bc3-rgba-unorm-srgb", "bc4-r-unorm", "bc4-r-snorm", "bc5-rg-unorm", "bc5-rg-snorm", "bc6h-rgb-ufloat", "bc6h-rgb-float", "bc7-rgba-unorm", "bc7-rgba-unorm-srgb", "etc2-rgb8unorm", "etc2-rgb8unorm-srgb", "etc2-rgb8a1unorm", "etc2-rgb8a1unorm-srgb", "etc2-rgba8unorm", "etc2-rgba8unorm-srgb", "eac-r11unorm", "eac-r11snorm", "eac-rg11unorm", "eac-rg11snorm", "astc-4x4-unorm", "astc-4x4-unorm-srgb", "astc-5x4-unorm", "astc-5x4-unorm-srgb", "astc-5x5-unorm", "astc-5x5-unorm-srgb", "astc-6x5-unorm", "astc-6x5-unorm-srgb", "astc-6x6-unorm", "astc-6x6-unorm-srgb", "astc-8x5-unorm", "astc-8x5-unorm-srgb", "astc-8x6-unorm", "astc-8x6-unorm-srgb", "astc-8x8-unorm", "astc-8x8-unorm-srgb", "astc-10x5-unorm", "astc-10x5-unorm-srgb", "astc-10x6-unorm", "astc-10x6-unorm-srgb", "astc-10x8-unorm", "astc-10x8-unorm-srgb", "astc-10x10-unorm", "astc-10x10-unorm-srgb", "astc-12x10-unorm", "astc-12x10-unorm-srgb", "astc-12x12-unorm", "astc-12x12-unorm-srgb" ],
  TextureSampleType: [ , , "float", "unfilterable-float", "depth", "sint", "uint" ],
  TextureViewDimension: [ , "1d", "2d", "2d-array", "cube", "cube-array", "3d" ],
  ToneMappingMode: [ , "standard", "extended" ],
  VertexFormat: [ , "uint8", "uint8x2", "uint8x4", "sint8", "sint8x2", "sint8x4", "unorm8", "unorm8x2", "unorm8x4", "snorm8", "snorm8x2", "snorm8x4", "uint16", "uint16x2", "uint16x4", "sint16", "sint16x2", "sint16x4", "unorm16", "unorm16x2", "unorm16x4", "snorm16", "snorm16x2", "snorm16x4", "float16", "float16x2", "float16x4", "float32", "float32x2", "float32x3", "float32x4", "uint32", "uint32x2", "uint32x3", "uint32x4", "sint32", "sint32x2", "sint32x3", "sint32x4", "unorm10-10-10-2", "unorm8x4-bgra", "snorm10-10-10-2" ],
  VertexStepMode: [ , "vertex", "instance" ],
  WGSLLanguageFeatureName: [ , "readonly_and_readwrite_storage_textures", "packed_4x8_integer_dot_product", "unrestricted_pointer_parameters", "pointer_composite_access", "uniform_buffer_standard_layout", "subgroup_id", "texture_and_sampler_let", "subgroup_uniformity", "texture_formats_tier1", "linear_indexing", "immediate_address_space", "buffer_view", "swizzle_assignment", "fragment_depth" ]
};

var _emscripten_webgpu_get_device = function() {
  var ret = (() => {
    assert(Module["preinitializedWebGPUDevice"]);
    if (WebGPU.preinitializedDeviceId === undefined) {
      WebGPU.preinitializedDeviceId = WebGPU.importJsDevice(Module["preinitializedWebGPUDevice"]);
      // Some users depend on this keeping the device alive, so we add an
      // additional reference when we first initialize it.
      _wgpuDeviceAddRef(WebGPU.preinitializedDeviceId);
    }
    _wgpuDeviceAddRef(WebGPU.preinitializedDeviceId);
    return WebGPU.preinitializedDeviceId;
  })();
  return BigInt(ret);
};

var emwgpuStringToInt_DeviceLostReason = {
  "undefined": 1,
  // For older browsers
  "unknown": 1,
  "destroyed": 2
};

function _emwgpuAdapterRequestDevice(adapterPtr, futureId, deviceLostFutureId, devicePtr, queuePtr, descriptor) {
  adapterPtr = bigintToI53Checked(adapterPtr);
  futureId = bigintToI53Checked(futureId);
  deviceLostFutureId = bigintToI53Checked(deviceLostFutureId);
  devicePtr = bigintToI53Checked(devicePtr);
  queuePtr = bigintToI53Checked(queuePtr);
  descriptor = bigintToI53Checked(descriptor);
  var adapter = WebGPU.getJsObject(adapterPtr);
  var desc = {};
  if (descriptor) {
    assert(descriptor);
    assert(Number((growMemViews(), HEAPU64)[((descriptor) >> 3)]) === 0);
    var requiredFeatureCount = Number((growMemViews(), HEAPU64)[(((descriptor) + (24)) >> 3)]);
    if (requiredFeatureCount) {
      var requiredFeaturesPtr = Number((growMemViews(), HEAPU64)[(((descriptor) + (32)) >> 3)]);
      // requiredFeaturesPtr is a pointer to an array of FeatureName which is an enum of size uint32_t
      desc["requiredFeatures"] = Array.from((growMemViews(), HEAPU32).subarray((((requiredFeaturesPtr) >> 2)), ((requiredFeaturesPtr + requiredFeatureCount * 4) >> 2)), feature => WebGPU.FeatureName[feature]);
    }
    var limitsPtr = Number((growMemViews(), HEAPU64)[(((descriptor) + (40)) >> 3)]);
    if (limitsPtr) {
      assert(limitsPtr);
      var nextInChainPtr = Number((growMemViews(), HEAPU64)[((limitsPtr) >> 3)]);
      var requiredLimits = {};
      function setLimitU32IfDefined(name, basePtr, limitOffset, ignoreIfZero = false) {
        var ptr = basePtr + limitOffset;
        var value = (growMemViews(), HEAPU32)[((ptr) >> 2)];
        if (value != 4294967295 && (!ignoreIfZero || value != 0)) {
          requiredLimits[name] = value;
        }
      }
      function setLimitU64IfDefined(name, basePtr, limitOffset) {
        var ptr = basePtr + limitOffset;
        // Handle WGPU_LIMIT_U64_UNDEFINED.
        var limitPart1 = (growMemViews(), HEAPU32)[((ptr) >> 2)];
        var limitPart2 = (growMemViews(), HEAPU32)[(((ptr) + (4)) >> 2)];
        if (limitPart1 != 4294967295 || limitPart2 != 4294967295) {
          requiredLimits[name] = readI53FromI64(ptr);
        }
      }
      setLimitU32IfDefined("maxTextureDimension1D", limitsPtr, 8);
      setLimitU32IfDefined("maxTextureDimension2D", limitsPtr, 12);
      setLimitU32IfDefined("maxTextureDimension3D", limitsPtr, 16);
      setLimitU32IfDefined("maxTextureArrayLayers", limitsPtr, 20);
      setLimitU32IfDefined("maxBindGroups", limitsPtr, 24);
      setLimitU32IfDefined("maxBindGroupsPlusVertexBuffers", limitsPtr, 28);
      setLimitU32IfDefined("maxBindingsPerBindGroup", limitsPtr, 32);
      setLimitU32IfDefined("maxDynamicUniformBuffersPerPipelineLayout", limitsPtr, 36);
      setLimitU32IfDefined("maxDynamicStorageBuffersPerPipelineLayout", limitsPtr, 40);
      setLimitU32IfDefined("maxSampledTexturesPerShaderStage", limitsPtr, 44);
      setLimitU32IfDefined("maxSamplersPerShaderStage", limitsPtr, 48);
      setLimitU32IfDefined("maxStorageBuffersPerShaderStage", limitsPtr, 52);
      setLimitU32IfDefined("maxStorageTexturesPerShaderStage", limitsPtr, 56);
      setLimitU32IfDefined("maxUniformBuffersPerShaderStage", limitsPtr, 60);
      setLimitU32IfDefined("minUniformBufferOffsetAlignment", limitsPtr, 80);
      setLimitU32IfDefined("minStorageBufferOffsetAlignment", limitsPtr, 84);
      setLimitU64IfDefined("maxUniformBufferBindingSize", limitsPtr, 64);
      setLimitU64IfDefined("maxStorageBufferBindingSize", limitsPtr, 72);
      setLimitU32IfDefined("maxVertexBuffers", limitsPtr, 88);
      setLimitU64IfDefined("maxBufferSize", limitsPtr, 96);
      setLimitU32IfDefined("maxVertexAttributes", limitsPtr, 104);
      setLimitU32IfDefined("maxVertexBufferArrayStride", limitsPtr, 108);
      setLimitU32IfDefined("maxInterStageShaderVariables", limitsPtr, 112);
      setLimitU32IfDefined("maxColorAttachments", limitsPtr, 116);
      setLimitU32IfDefined("maxColorAttachmentBytesPerSample", limitsPtr, 120);
      setLimitU32IfDefined("maxComputeWorkgroupStorageSize", limitsPtr, 124);
      setLimitU32IfDefined("maxComputeInvocationsPerWorkgroup", limitsPtr, 128);
      setLimitU32IfDefined("maxComputeWorkgroupSizeX", limitsPtr, 132);
      setLimitU32IfDefined("maxComputeWorkgroupSizeY", limitsPtr, 136);
      setLimitU32IfDefined("maxComputeWorkgroupSizeZ", limitsPtr, 140);
      setLimitU32IfDefined("maxComputeWorkgroupsPerDimension", limitsPtr, 144);
      // Not present in all browsers. If the app requested 0, avoid passing it through so it won't cause an error.
      setLimitU32IfDefined("maxImmediateSize", limitsPtr, 148, true);
      if (nextInChainPtr !== 0) {
        var sType = (growMemViews(), HEAP32)[(((nextInChainPtr) + (8)) >> 2)];
        assert(sType === 15);
        assert(0 === Number((growMemViews(), HEAPU64)[((nextInChainPtr) >> 3)]));
        var compatibilityModeLimitsPtr = nextInChainPtr;
        assert(compatibilityModeLimitsPtr);
        assert(Number((growMemViews(), HEAPU64)[((compatibilityModeLimitsPtr) >> 3)]) === 0);
        // If not present in the browser, don't request these, otherwise they'll cause an error.
        // (Technically, if any of these is higher than the PerShaderStage equivalent, we should
        // raise the PerShaderStage limit instead, but that's complex and apps should be able to
        // deal with that themselves.)
        if ("maxStorageBuffersInVertexStage" in GPUSupportedLimits.prototype) {
          setLimitU32IfDefined("maxStorageBuffersInVertexStage", compatibilityModeLimitsPtr, 16);
          setLimitU32IfDefined("maxStorageTexturesInVertexStage", compatibilityModeLimitsPtr, 20);
          setLimitU32IfDefined("maxStorageBuffersInFragmentStage", compatibilityModeLimitsPtr, 24);
          setLimitU32IfDefined("maxStorageTexturesInFragmentStage", compatibilityModeLimitsPtr, 28);
        }
      }
      desc["requiredLimits"] = requiredLimits;
    }
    var defaultQueuePtr = Number((growMemViews(), HEAPU64)[(((descriptor) + (48)) >> 3)]);
    if (defaultQueuePtr) {
      var defaultQueueDesc = {
        "label": WebGPU.makeStringFromOptionalStringView(defaultQueuePtr + 8)
      };
      desc["defaultQueue"] = defaultQueueDesc;
    }
    desc["label"] = WebGPU.makeStringFromOptionalStringView(descriptor + 8);
  }
  runtimeKeepalivePush();
  // requestDevice
  WebGPU.Internals.futureInsert(futureId, adapter.requestDevice(desc).then(device => {
    runtimeKeepalivePop();
    // requestDevice fulfilled
    callUserCallback(() => {
      WebGPU.Internals.jsObjectInsert(queuePtr, device.queue);
      WebGPU.Internals.jsObjectInsert(devicePtr, device);
      devicePtr = BigInt(devicePtr);
      // Set up device lost promise resolution.
      assert(deviceLostFutureId);
      // Don't keepalive here, because this isn't guaranteed to ever happen.
      WebGPU.Internals.futureInsert(deviceLostFutureId, device.lost.then(info => {
        // If the runtime has exited, avoid calling callUserCallback as it
        // will print an error (e.g. if the device got freed during shutdown).
        if (runtimeExited) return;
        callUserCallback(() => {
          // Unset the uncaptured error handler.
          device.onuncapturederror = ev => {};
          var sp = stackSave();
          var messagePtr = stringToUTF8OnStack(info.message);
          _emwgpuOnDeviceLostCompleted(deviceLostFutureId, emwgpuStringToInt_DeviceLostReason[info.reason], BigInt(messagePtr));
          stackRestore(sp);
        });
      }));
      // Set up uncaptured error handlers.
      assert(typeof GPUValidationError != "undefined");
      assert(typeof GPUOutOfMemoryError != "undefined");
      assert(typeof GPUInternalError != "undefined");
      device.onuncapturederror = ev => {
        var type = 5;
        if (ev.error instanceof GPUValidationError) type = 2; else if (ev.error instanceof GPUOutOfMemoryError) type = 3; else if (ev.error instanceof GPUInternalError) type = 4;
        var sp = stackSave();
        var messagePtr = stringToUTF8OnStack(ev.error.message);
        _emwgpuOnUncapturedError(BigInt(devicePtr), type, BigInt(messagePtr));
        stackRestore(sp);
      };
      _emwgpuOnRequestDeviceCompleted(futureId, 1, BigInt(devicePtr), 0n);
    });
  }, ex => {
    runtimeKeepalivePop();
    // requestDevice rejected
    callUserCallback(() => {
      var sp = stackSave();
      var messagePtr = stringToUTF8OnStack(ex.message);
      _emwgpuOnRequestDeviceCompleted(futureId, 3, BigInt(devicePtr), BigInt(messagePtr));
      if (deviceLostFutureId) {
        _emwgpuOnDeviceLostCompleted(deviceLostFutureId, 4, BigInt(messagePtr));
      }
      stackRestore(sp);
    });
  }));
}

function _emwgpuBufferDestroy(bufferPtr) {
  bufferPtr = bigintToI53Checked(bufferPtr);
  var buffer = WebGPU.getJsObject(bufferPtr);
  var onUnmap = WebGPU.Internals.bufferOnUnmaps[bufferPtr];
  if (onUnmap) {
    for (var i = 0; i < onUnmap.length; ++i) {
      onUnmap[i]();
    }
    delete WebGPU.Internals.bufferOnUnmaps[bufferPtr];
  }
  buffer.destroy();
}

var _emwgpuBufferGetConstMappedRange = function(bufferPtr, offset, size) {
  bufferPtr = bigintToI53Checked(bufferPtr);
  offset = bigintToI53Checked(offset);
  size = bigintToI53Checked(size);
  var ret = (() => {
    var buffer = WebGPU.getJsObject(bufferPtr);
    if (size === 0) warnOnce("getMappedRange size=0 no longer means WGPU_WHOLE_MAP_SIZE");
    if (size == -1) size = undefined;
    var mapped;
    try {
      mapped = buffer.getMappedRange(offset, size);
    } catch (ex) {
      err(`buffer.getMappedRange(${offset}, ${size}) failed: ${ex}`);
      return 0n;
    }
    var data = _memalign(16, mapped.byteLength);
    (growMemViews(), HEAPU8).set(new Uint8Array(mapped), data);
    WebGPU.Internals.bufferOnUnmaps[bufferPtr].push(() => _free(data));
    return data;
  })();
  return BigInt(ret);
};

var _emwgpuBufferGetMappedRange = function(bufferPtr, offset, size) {
  bufferPtr = bigintToI53Checked(bufferPtr);
  offset = bigintToI53Checked(offset);
  size = bigintToI53Checked(size);
  var ret = (() => {
    var buffer = WebGPU.getJsObject(bufferPtr);
    if (size === 0) warnOnce("getMappedRange size=0 no longer means WGPU_WHOLE_MAP_SIZE");
    if (size == -1) size = undefined;
    var mapped;
    try {
      mapped = buffer.getMappedRange(offset, size);
    } catch (ex) {
      err(`buffer.getMappedRange(${offset}, ${size}) failed: ${ex}`);
      return 0n;
    }
    var data = _memalign(16, mapped.byteLength);
    (growMemViews(), HEAPU8).fill(0, data, mapped.byteLength);
    WebGPU.Internals.bufferOnUnmaps[bufferPtr].push(() => {
      new Uint8Array(mapped).set((growMemViews(), HEAPU8).subarray(data, data + mapped.byteLength));
      _free(data);
    });
    return data;
  })();
  return BigInt(ret);
};

var _emwgpuBufferMapAsync = function(bufferPtr, futureId, mode, offset, size) {
  bufferPtr = bigintToI53Checked(bufferPtr);
  futureId = bigintToI53Checked(futureId);
  mode = bigintToI53Checked(mode);
  offset = bigintToI53Checked(offset);
  size = bigintToI53Checked(size);
  var buffer = WebGPU.getJsObject(bufferPtr);
  WebGPU.Internals.bufferOnUnmaps[bufferPtr] = [];
  if (size == -1) size = undefined;
  runtimeKeepalivePush();
  // mapAsync
  WebGPU.Internals.futureInsert(futureId, buffer.mapAsync(mode, offset, size).then(() => {
    runtimeKeepalivePop();
    // mapAsync fulfilled
    callUserCallback(() => {
      _emwgpuOnMapAsyncCompleted(futureId, 1, 0n);
    });
  }, ex => {
    runtimeKeepalivePop();
    // mapAsync rejected
    callUserCallback(() => {
      var sp = stackSave();
      var messagePtr = stringToUTF8OnStack(ex.message);
      var status = ex.name === "AbortError" ? 4 : ex.name === "OperationError" ? 3 : 0;
      assert(status);
      _emwgpuOnMapAsyncCompleted(futureId, status, BigInt(messagePtr));
      delete WebGPU.Internals.bufferOnUnmaps[bufferPtr];
    });
  }));
};

function _emwgpuBufferUnmap(bufferPtr) {
  bufferPtr = bigintToI53Checked(bufferPtr);
  var buffer = WebGPU.getJsObject(bufferPtr);
  var onUnmap = WebGPU.Internals.bufferOnUnmaps[bufferPtr];
  if (!onUnmap) {
    // Already unmapped
    return;
  }
  for (var i = 0; i < onUnmap.length; ++i) {
    onUnmap[i]();
  }
  delete WebGPU.Internals.bufferOnUnmaps[bufferPtr];
  buffer.unmap();
}

function _emwgpuDelete(ptr) {
  ptr = bigintToI53Checked(ptr);
  delete WebGPU.Internals.jsObjects[ptr];
}

function _emwgpuDeviceCreateBuffer(devicePtr, descriptor, bufferPtr) {
  devicePtr = bigintToI53Checked(devicePtr);
  descriptor = bigintToI53Checked(descriptor);
  bufferPtr = bigintToI53Checked(bufferPtr);
  assert(descriptor);
  assert(Number((growMemViews(), HEAPU64)[((descriptor) >> 3)]) === 0);
  var mappedAtCreation = !!((growMemViews(), HEAPU32)[(((descriptor) + (40)) >> 2)]);
  var desc = {
    "label": WebGPU.makeStringFromOptionalStringView(descriptor + 8),
    "usage": (growMemViews(), HEAPU32)[(((descriptor) + (24)) >> 2)],
    "size": readI53FromI64((descriptor) + (32)),
    "mappedAtCreation": mappedAtCreation
  };
  var device = WebGPU.getJsObject(devicePtr);
  var buffer;
  try {
    buffer = device.createBuffer(desc);
  } catch (ex) {
    // The only exception should be RangeError if mapping at creation ran out of memory.
    assert(ex instanceof RangeError);
    assert(mappedAtCreation);
    err("createBuffer threw:", ex);
    return false;
  }
  WebGPU.Internals.jsObjectInsert(bufferPtr, buffer);
  if (mappedAtCreation) {
    WebGPU.Internals.bufferOnUnmaps[bufferPtr] = [];
  }
  return true;
}

var _emwgpuDeviceCreateComputePipelineAsync = function(devicePtr, futureId, descriptor, pipelinePtr) {
  devicePtr = bigintToI53Checked(devicePtr);
  futureId = bigintToI53Checked(futureId);
  descriptor = bigintToI53Checked(descriptor);
  pipelinePtr = bigintToI53Checked(pipelinePtr);
  var desc = WebGPU.makeComputePipelineDesc(descriptor);
  var device = WebGPU.getJsObject(devicePtr);
  runtimeKeepalivePush();
  // createComputePipelineAsync
  WebGPU.Internals.futureInsert(futureId, device.createComputePipelineAsync(desc).then(pipeline => {
    runtimeKeepalivePop();
    // createComputePipelineAsync fulfilled
    callUserCallback(() => {
      WebGPU.Internals.jsObjectInsert(pipelinePtr, pipeline);
      _emwgpuOnCreateComputePipelineCompleted(futureId, 1, BigInt(pipelinePtr), 0n);
    });
  }, pipelineError => {
    runtimeKeepalivePop();
    // createComputePipelineAsync rejected
    callUserCallback(() => {
      var sp = stackSave();
      // UE 5.9 change: any rejection is a failed pipeline. A reason other than validation (WebKit rejects with none, and
      // not always with a GPUPipelineError) is an internal error; it was 0, which failed an assertion and aborted the game
      var messagePtr = stringToUTF8OnStack(String((pipelineError && pipelineError.message) || pipelineError));
      var status = (pipelineError && pipelineError.reason === "validation") ? 3 : 4;
      _emwgpuOnCreateComputePipelineCompleted(futureId, status, BigInt(pipelinePtr), BigInt(messagePtr));
      stackRestore(sp);
    });
  }));
};

var _emwgpuDeviceCreateRenderPipelineAsync = function(devicePtr, futureId, descriptor, pipelinePtr) {
  devicePtr = bigintToI53Checked(devicePtr);
  futureId = bigintToI53Checked(futureId);
  descriptor = bigintToI53Checked(descriptor);
  pipelinePtr = bigintToI53Checked(pipelinePtr);
  var desc = WebGPU.makeRenderPipelineDesc(descriptor);
  var device = WebGPU.getJsObject(devicePtr);
  runtimeKeepalivePush();
  // createRenderPipelineAsync
  WebGPU.Internals.futureInsert(futureId, device.createRenderPipelineAsync(desc).then(pipeline => {
    runtimeKeepalivePop();
    // createRenderPipelineAsync fulfilled
    callUserCallback(() => {
      WebGPU.Internals.jsObjectInsert(pipelinePtr, pipeline);
      _emwgpuOnCreateRenderPipelineCompleted(futureId, 1, BigInt(pipelinePtr), 0n);
    });
  }, pipelineError => {
    runtimeKeepalivePop();
    // createRenderPipelineAsync rejected
    callUserCallback(() => {
      var sp = stackSave();
      // UE 5.9 change: any rejection is a failed pipeline. A reason other than validation (WebKit rejects with none, and
      // not always with a GPUPipelineError) is an internal error; it was 0, which failed an assertion and aborted the game
      var messagePtr = stringToUTF8OnStack(String((pipelineError && pipelineError.message) || pipelineError));
      var status = (pipelineError && pipelineError.reason === "validation") ? 3 : 4;
      _emwgpuOnCreateRenderPipelineCompleted(futureId, status, BigInt(pipelinePtr), BigInt(messagePtr));
      stackRestore(sp);
    });
  }));
};

function _emwgpuDeviceCreateShaderModule(devicePtr, descriptor, shaderModulePtr) {
  devicePtr = bigintToI53Checked(devicePtr);
  descriptor = bigintToI53Checked(descriptor);
  shaderModulePtr = bigintToI53Checked(shaderModulePtr);
  assert(descriptor);
  var nextInChainPtr = Number((growMemViews(), HEAPU64)[((descriptor) >> 3)]);
  assert(nextInChainPtr !== 0);
  var sType = (growMemViews(), HEAP32)[(((nextInChainPtr) + (8)) >> 2)];
  var desc = {
    "label": WebGPU.makeStringFromOptionalStringView(descriptor + 8),
    "code": ""
  };
  switch (sType) {
   case 2:
    {
      desc["code"] = WebGPU.makeStringFromStringView(nextInChainPtr + 16);
      break;
    }

   default:
    abort("unrecognized ShaderModule sType");
  }
  var device = WebGPU.getJsObject(devicePtr);
  WebGPU.Internals.jsObjectInsert(shaderModulePtr, device.createShaderModule(desc));
}

var _emwgpuDeviceDestroy = devicePtr => {
  const device = WebGPU.getJsObject(devicePtr);
  // Remove the onuncapturederror handler which holds a pointer to the WGPUDevice.
  device.onuncapturederror = null;
  device.destroy();
};

var _emwgpuDevicePopErrorScope = function(devicePtr, futureId) {
  devicePtr = bigintToI53Checked(devicePtr);
  futureId = bigintToI53Checked(futureId);
  var device = WebGPU.getJsObject(devicePtr);
  runtimeKeepalivePush();
  // popErrorScope
  WebGPU.Internals.futureInsert(futureId, device.popErrorScope().then(gpuError => {
    runtimeKeepalivePop();
    // popErrorScope fulfilled
    callUserCallback(() => {
      var type = 5;
      if (!gpuError) type = 1; else if (gpuError instanceof GPUValidationError) type = 2; else if (gpuError instanceof GPUOutOfMemoryError) type = 3; else if (gpuError instanceof GPUInternalError) type = 4; else assert(false);
      var sp = stackSave();
      var messagePtr = gpuError ? stringToUTF8OnStack(gpuError.message) : 0;
      _emwgpuOnPopErrorScopeCompleted(futureId, 1, type, BigInt(messagePtr));
      stackRestore(sp);
    });
  }, ex => {
    runtimeKeepalivePop();
    // popErrorScope rejected
    callUserCallback(() => {
      var sp = stackSave();
      var messagePtr = stringToUTF8OnStack(ex.message);
      _emwgpuOnPopErrorScopeCompleted(futureId, 1, 5, BigInt(messagePtr));
      stackRestore(sp);
    });
  }));
};

var emwgpuStringToInt_PreferredFormat = {
  "rgba8unorm": 22,
  "bgra8unorm": 27
};

var _emwgpuGetPreferredFormat = () => {
  var format = navigator.gpu.getPreferredCanvasFormat();
  return emwgpuStringToInt_PreferredFormat[format];
};

function _emwgpuInstanceRequestAdapter(instancePtr, futureId, options, adapterPtr) {
  instancePtr = bigintToI53Checked(instancePtr);
  futureId = bigintToI53Checked(futureId);
  options = bigintToI53Checked(options);
  adapterPtr = bigintToI53Checked(adapterPtr);
  var opts;
  if (options) {
    assert(options);
    opts = {
      "featureLevel": WebGPU.FeatureLevel[(growMemViews(), HEAP32)[(((options) + (8)) >> 2)]],
      "powerPreference": WebGPU.PowerPreference[(growMemViews(), HEAP32)[(((options) + (12)) >> 2)]],
      "forceFallbackAdapter": !!((growMemViews(), HEAPU32)[(((options) + (16)) >> 2)])
    };
    var nextInChainPtr = Number((growMemViews(), HEAPU64)[((options) >> 3)]);
    if (nextInChainPtr !== 0) {
      var sType = (growMemViews(), HEAP32)[(((nextInChainPtr) + (8)) >> 2)];
      assert(sType === 11);
      assert(0 === Number((growMemViews(), HEAPU64)[((nextInChainPtr) >> 3)]));
      var webxrOptions = nextInChainPtr;
      assert(webxrOptions);
      assert(Number((growMemViews(), HEAPU64)[((webxrOptions) >> 3)]) === 0);
      opts.xrCompatible = !!((growMemViews(), HEAPU32)[(((webxrOptions) + (16)) >> 2)]);
    }
  }
  if (!("gpu" in navigator)) {
    var sp = stackSave();
    var messagePtr = stringToUTF8OnStack("WebGPU not available on this browser (navigator.gpu is not available)");
    _emwgpuOnRequestAdapterCompleted(futureId, 3, BigInt(adapterPtr), BigInt(messagePtr));
    stackRestore(sp);
    return;
  }
  runtimeKeepalivePush();
  // requestAdapter
  WebGPU.Internals.futureInsert(futureId, navigator.gpu.requestAdapter(opts).then(adapter => {
    runtimeKeepalivePop();
    // requestAdapter fulfilled
    callUserCallback(() => {
      if (adapter) {
        WebGPU.Internals.jsObjectInsert(adapterPtr, adapter);
        _emwgpuOnRequestAdapterCompleted(futureId, 1, BigInt(adapterPtr), 0n);
      } else {
        var sp = stackSave();
        var messagePtr = stringToUTF8OnStack("WebGPU not available on this browser (requestAdapter returned null)");
        _emwgpuOnRequestAdapterCompleted(futureId, 3, BigInt(adapterPtr), BigInt(messagePtr));
        stackRestore(sp);
      }
    });
  }, ex => {
    runtimeKeepalivePop();
    // requestAdapter rejected
    callUserCallback(() => {
      var sp = stackSave();
      var messagePtr = stringToUTF8OnStack(ex.message);
      _emwgpuOnRequestAdapterCompleted(futureId, 4, BigInt(adapterPtr), BigInt(messagePtr));
      stackRestore(sp);
    });
  }));
}

var _emwgpuQueueOnSubmittedWorkDone = function(queuePtr, futureId) {
  queuePtr = bigintToI53Checked(queuePtr);
  futureId = bigintToI53Checked(futureId);
  var queue = WebGPU.getJsObject(queuePtr);
  runtimeKeepalivePush();
  // onSubmittedWorkDone
  WebGPU.Internals.futureInsert(futureId, queue.onSubmittedWorkDone().then(() => {
    runtimeKeepalivePop();
    // onSubmittedWorkDone fulfilled (assumed not to reject)
    callUserCallback(() => {
      _emwgpuOnWorkDoneCompleted(futureId, 1);
    });
  }));
};

var ENV = {};

var getExecutableName = () => thisProgram;

var getEnvStrings = () => {
  if (!getEnvStrings.strings) {
    // Default values.
    var lang = (globalThis.navigator?.language ?? "C").replace("-", "_") + ".UTF-8";
    var env = {
      "USER": "web_user",
      "LOGNAME": "web_user",
      "PATH": "/",
      "PWD": "/",
      "HOME": "/home/web_user",
      "LANG": lang,
      "_": getExecutableName()
    };
    // Apply the user-provided values, if any.
    for (var x in ENV) {
      // x is a key in ENV; if ENV[x] is undefined, that means it was
      // explicitly set to be so. We allow user code to do that to
      // force variables with default values to remain unset.
      if (ENV[x] === undefined) delete env[x]; else env[x] = ENV[x];
    }
    var strings = [];
    for (var x in env) {
      strings.push(`${x}=${env[x]}`);
    }
    getEnvStrings.strings = strings;
  }
  return getEnvStrings.strings;
};

function _environ_get(__environ, environ_buf) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(19, 0, 1, __environ, environ_buf);
  __environ = bigintToI53Checked(__environ);
  environ_buf = bigintToI53Checked(environ_buf);
  var bufSize = 0;
  var envp = 0;
  for (var string of getEnvStrings()) {
    var ptr = environ_buf + bufSize;
    (growMemViews(), HEAPU64)[(((__environ) + (envp)) >> 3)] = BigInt(ptr);
    bufSize += stringToUTF8(string, ptr, Infinity) + 1;
    envp += 8;
  }
  return 0;
}

function _environ_sizes_get(penviron_count, penviron_buf_size) {
  if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(20, 0, 1, penviron_count, penviron_buf_size);
  penviron_count = bigintToI53Checked(penviron_count);
  penviron_buf_size = bigintToI53Checked(penviron_buf_size);
  var strings = getEnvStrings();
  (growMemViews(), HEAPU64)[((penviron_count) >> 3)] = BigInt(strings.length);
  var bufSize = 0;
  for (var string of strings) {
    bufSize += lengthBytesUTF8(string) + 1;
  }
  (growMemViews(), HEAPU64)[((penviron_buf_size) >> 3)] = BigInt(bufSize);
  return 0;
}

var initRandomFill = () => view => (view.set(crypto.getRandomValues(new Uint8Array(view.byteLength))), 
0);

var randomFill = view => (randomFill = initRandomFill())(view);

function _random_get(buffer, size) {
  buffer = bigintToI53Checked(buffer);
  size = bigintToI53Checked(size);
  return randomFill((growMemViews(), HEAPU8).subarray(buffer, buffer + size));
}

var emwgpuStringToInt_FeatureName = {
  "core-features-and-limits": 1,
  "depth-clip-control": 2,
  "depth32float-stencil8": 3,
  "texture-compression-bc": 4,
  "texture-compression-bc-sliced-3d": 5,
  "texture-compression-etc2": 6,
  "texture-compression-astc": 7,
  "texture-compression-astc-sliced-3d": 8,
  "timestamp-query": 9,
  "indirect-first-instance": 10,
  "shader-f16": 11,
  "rg11b10ufloat-renderable": 12,
  "bgra8unorm-storage": 13,
  "float32-filterable": 14,
  "float32-blendable": 15,
  "clip-distances": 16,
  "dual-source-blending": 17,
  "subgroups": 18,
  "texture-formats-tier1": 19,
  "texture-formats-tier2": 20,
  "primitive-index": 21,
  "texture-component-swizzle": 22,
  "subgroup-size-control": 23,
  "chromium-experimental-unorm16-texture-formats": 327692,
  "chromium-experimental-multi-draw-indirect": 327729,
  "chromium-experimental-subgroup-matrix": 327732
};

function _wgpuAdapterGetFeatures(adapterPtr, supportedFeatures) {
  adapterPtr = bigintToI53Checked(adapterPtr);
  supportedFeatures = bigintToI53Checked(supportedFeatures);
  var adapter = WebGPU.getJsObject(adapterPtr);
  // Always allocate enough space for all the features, though some may be unused.
  var featuresPtr = _malloc(adapter.features.size * 4);
  var offset = 0;
  var numFeatures = 0;
  for (const feature of adapter.features) {
    var featureEnumValue = emwgpuStringToInt_FeatureName[feature];
    if (featureEnumValue >= 0) {
      (growMemViews(), HEAP32)[(((featuresPtr) + (offset)) >> 2)] = featureEnumValue;
      offset += 4;
      numFeatures++;
    }
  }
  (growMemViews(), HEAPU64)[(((supportedFeatures) + (8)) >> 3)] = BigInt(featuresPtr);
  (growMemViews(), HEAPU64)[((supportedFeatures) >> 3)] = BigInt(numFeatures);
}

function _wgpuAdapterGetInfo(adapterPtr, info) {
  adapterPtr = bigintToI53Checked(adapterPtr);
  info = bigintToI53Checked(info);
  var adapter = WebGPU.getJsObject(adapterPtr);
  WebGPU.fillAdapterInfoStruct(adapter.info, info);
  return 1;
}

function _wgpuAdapterGetLimits(adapterPtr, limitsOutPtr) {
  adapterPtr = bigintToI53Checked(adapterPtr);
  limitsOutPtr = bigintToI53Checked(limitsOutPtr);
  var adapter = WebGPU.getJsObject(adapterPtr);
  WebGPU.fillLimitStruct(adapter.limits, limitsOutPtr);
  return 1;
}

function _wgpuAdapterHasFeature(adapterPtr, featureEnumValue) {
  adapterPtr = bigintToI53Checked(adapterPtr);
  var adapter = WebGPU.getJsObject(adapterPtr);
  return adapter.features.has(WebGPU.FeatureName[featureEnumValue]);
}

var _wgpuCommandEncoderBeginComputePass = function(encoderPtr, descriptor) {
  encoderPtr = bigintToI53Checked(encoderPtr);
  descriptor = bigintToI53Checked(descriptor);
  var ret = (() => {
    var desc;
    if (descriptor) {
      assert(descriptor);
      assert(Number((growMemViews(), HEAPU64)[((descriptor) >> 3)]) === 0);
      desc = {
        "label": WebGPU.makeStringFromOptionalStringView(descriptor + 8),
        "timestampWrites": WebGPU.makePassTimestampWrites(Number((growMemViews(), HEAPU64)[(((descriptor) + (24)) >> 3)]))
      };
    }
    var commandEncoder = WebGPU.getJsObject(encoderPtr);
    var ptr = _emwgpuCreateComputePassEncoder(0n);
    WebGPU.Internals.jsObjectInsert(ptr, commandEncoder.beginComputePass(desc));
    return ptr;
  })();
  return BigInt(ret);
};

var _wgpuCommandEncoderBeginRenderPass = function(encoderPtr, descriptor) {
  encoderPtr = bigintToI53Checked(encoderPtr);
  descriptor = bigintToI53Checked(descriptor);
  var ret = (() => {
    assert(descriptor);
    function makeColorAttachment(caPtr) {
      var viewPtr = Number((growMemViews(), HEAPU64)[(((caPtr) + (8)) >> 3)]);
      if (viewPtr === 0) {
        // Null `view` means no attachment in this slot.
        return undefined;
      }
      var depthSlice = (growMemViews(), HEAPU32)[(((caPtr) + (16)) >> 2)];
      if (depthSlice == 4294967295) depthSlice = undefined;
      return {
        "view": WebGPU.getJsObject(viewPtr),
        "depthSlice": depthSlice,
        "resolveTarget": WebGPU.getJsObject(Number((growMemViews(), HEAPU64)[(((caPtr) + (24)) >> 3)])),
        "clearValue": WebGPU.makeColor(caPtr + 40),
        "loadOp": WebGPU.LoadOp[(growMemViews(), HEAP32)[(((caPtr) + (32)) >> 2)]],
        "storeOp": WebGPU.StoreOp[(growMemViews(), HEAP32)[(((caPtr) + (36)) >> 2)]]
      };
    }
    function makeColorAttachments(count, caPtr) {
      var attachments = [];
      for (var i = 0; i < count; ++i) {
        attachments.push(makeColorAttachment(caPtr + 72 * i));
      }
      return attachments;
    }
    function makeDepthStencilAttachment(dsaPtr) {
      if (dsaPtr === 0) return undefined;
      return {
        "view": WebGPU.getJsObject(Number((growMemViews(), HEAPU64)[(((dsaPtr) + (8)) >> 3)])),
        "depthClearValue": (growMemViews(), HEAPF32)[(((dsaPtr) + (24)) >> 2)],
        "depthLoadOp": WebGPU.LoadOp[(growMemViews(), HEAP32)[(((dsaPtr) + (16)) >> 2)]],
        "depthStoreOp": WebGPU.StoreOp[(growMemViews(), HEAP32)[(((dsaPtr) + (20)) >> 2)]],
        "depthReadOnly": !!((growMemViews(), HEAPU32)[(((dsaPtr) + (28)) >> 2)]),
        "stencilClearValue": (growMemViews(), HEAPU32)[(((dsaPtr) + (40)) >> 2)],
        "stencilLoadOp": WebGPU.LoadOp[(growMemViews(), HEAP32)[(((dsaPtr) + (32)) >> 2)]],
        "stencilStoreOp": WebGPU.StoreOp[(growMemViews(), HEAP32)[(((dsaPtr) + (36)) >> 2)]],
        "stencilReadOnly": !!((growMemViews(), HEAPU32)[(((dsaPtr) + (44)) >> 2)])
      };
    }
    function makeRenderPassDescriptor(descriptor) {
      assert(descriptor);
      var nextInChainPtr = Number((growMemViews(), HEAPU64)[((descriptor) >> 3)]);
      var maxDrawCount = undefined;
      if (nextInChainPtr !== 0) {
        var sType = (growMemViews(), HEAP32)[(((nextInChainPtr) + (8)) >> 2)];
        assert(sType === 3);
        assert(0 === Number((growMemViews(), HEAPU64)[((nextInChainPtr) >> 3)]));
        var renderPassMaxDrawCount = nextInChainPtr;
        assert(renderPassMaxDrawCount);
        assert(Number((growMemViews(), HEAPU64)[((renderPassMaxDrawCount) >> 3)]) === 0);
        // Note: The user could have passed a really huge value here, which is technically valid in
        // C but will not be allowed by WebGPU in JS because of [EnforceRange]. We intentionally
        // ignore that case because it's not useful - apps can just pick a smaller maxDrawCount.
        maxDrawCount = readI53FromI64((renderPassMaxDrawCount) + (16));
      }
      var desc = {
        "label": WebGPU.makeStringFromOptionalStringView(descriptor + 8),
        "colorAttachments": makeColorAttachments(Number((growMemViews(), HEAPU64)[(((descriptor) + (24)) >> 3)]), Number((growMemViews(), 
        HEAPU64)[(((descriptor) + (32)) >> 3)])),
        "depthStencilAttachment": makeDepthStencilAttachment(Number((growMemViews(), HEAPU64)[(((descriptor) + (40)) >> 3)])),
        "occlusionQuerySet": WebGPU.getJsObject(Number((growMemViews(), HEAPU64)[(((descriptor) + (48)) >> 3)])),
        "timestampWrites": WebGPU.makePassTimestampWrites(Number((growMemViews(), HEAPU64)[(((descriptor) + (56)) >> 3)])),
        "maxDrawCount": maxDrawCount
      };
      return desc;
    }
    var desc = makeRenderPassDescriptor(descriptor);
    var commandEncoder = WebGPU.getJsObject(encoderPtr);
    var ptr = _emwgpuCreateRenderPassEncoder(0n);
    WebGPU.Internals.jsObjectInsert(ptr, commandEncoder.beginRenderPass(desc));
    return ptr;
  })();
  return BigInt(ret);
};

function _wgpuCommandEncoderClearBuffer(encoderPtr, bufferPtr, offset, size) {
  encoderPtr = bigintToI53Checked(encoderPtr);
  bufferPtr = bigintToI53Checked(bufferPtr);
  offset = bigintToI53Checked(offset);
  size = bigintToI53Checked(size);
  var commandEncoder = WebGPU.getJsObject(encoderPtr);
  if (size == -1) size = undefined;
  var buffer = WebGPU.getJsObject(bufferPtr);
  commandEncoder.clearBuffer(buffer, offset, size);
}

function _wgpuCommandEncoderCopyBufferToBuffer(encoderPtr, srcPtr, srcOffset, dstPtr, dstOffset, size) {
  encoderPtr = bigintToI53Checked(encoderPtr);
  srcPtr = bigintToI53Checked(srcPtr);
  srcOffset = bigintToI53Checked(srcOffset);
  dstPtr = bigintToI53Checked(dstPtr);
  dstOffset = bigintToI53Checked(dstOffset);
  size = bigintToI53Checked(size);
  var commandEncoder = WebGPU.getJsObject(encoderPtr);
  var src = WebGPU.getJsObject(srcPtr);
  var dst = WebGPU.getJsObject(dstPtr);
  commandEncoder.copyBufferToBuffer(src, srcOffset, dst, dstOffset, size);
}

function _wgpuCommandEncoderCopyBufferToTexture(encoderPtr, srcPtr, dstPtr, copySizePtr) {
  encoderPtr = bigintToI53Checked(encoderPtr);
  srcPtr = bigintToI53Checked(srcPtr);
  dstPtr = bigintToI53Checked(dstPtr);
  copySizePtr = bigintToI53Checked(copySizePtr);
  var commandEncoder = WebGPU.getJsObject(encoderPtr);
  var copySize = WebGPU.makeExtent3D(copySizePtr);
  commandEncoder.copyBufferToTexture(WebGPU.makeTexelCopyBufferInfo(srcPtr), WebGPU.makeTexelCopyTextureInfo(dstPtr), copySize);
}

function _wgpuCommandEncoderCopyTextureToBuffer(encoderPtr, srcPtr, dstPtr, copySizePtr) {
  encoderPtr = bigintToI53Checked(encoderPtr);
  srcPtr = bigintToI53Checked(srcPtr);
  dstPtr = bigintToI53Checked(dstPtr);
  copySizePtr = bigintToI53Checked(copySizePtr);
  var commandEncoder = WebGPU.getJsObject(encoderPtr);
  var copySize = WebGPU.makeExtent3D(copySizePtr);
  commandEncoder.copyTextureToBuffer(WebGPU.makeTexelCopyTextureInfo(srcPtr), WebGPU.makeTexelCopyBufferInfo(dstPtr), copySize);
}

function _wgpuCommandEncoderCopyTextureToTexture(encoderPtr, srcPtr, dstPtr, copySizePtr) {
  encoderPtr = bigintToI53Checked(encoderPtr);
  srcPtr = bigintToI53Checked(srcPtr);
  dstPtr = bigintToI53Checked(dstPtr);
  copySizePtr = bigintToI53Checked(copySizePtr);
  var commandEncoder = WebGPU.getJsObject(encoderPtr);
  var copySize = WebGPU.makeExtent3D(copySizePtr);
  commandEncoder.copyTextureToTexture(WebGPU.makeTexelCopyTextureInfo(srcPtr), WebGPU.makeTexelCopyTextureInfo(dstPtr), copySize);
}

var _wgpuCommandEncoderFinish = function(encoderPtr, descriptor) {
  encoderPtr = bigintToI53Checked(encoderPtr);
  descriptor = bigintToI53Checked(descriptor);
  var ret = (() => {
    // TODO: Use the descriptor.
    var commandEncoder = WebGPU.getJsObject(encoderPtr);
    var ptr = _emwgpuCreateCommandBuffer(0n);
    WebGPU.Internals.jsObjectInsert(ptr, commandEncoder.finish());
    return ptr;
  })();
  return BigInt(ret);
};

function _wgpuCommandEncoderResolveQuerySet(encoderPtr, querySetPtr, firstQuery, queryCount, destinationPtr, destinationOffset) {
  encoderPtr = bigintToI53Checked(encoderPtr);
  querySetPtr = bigintToI53Checked(querySetPtr);
  destinationPtr = bigintToI53Checked(destinationPtr);
  destinationOffset = bigintToI53Checked(destinationOffset);
  assert(firstQuery >= 0);
  assert(queryCount >= 0);
  var commandEncoder = WebGPU.getJsObject(encoderPtr);
  var querySet = WebGPU.getJsObject(querySetPtr);
  var destination = WebGPU.getJsObject(destinationPtr);
  commandEncoder.resolveQuerySet(querySet, firstQuery, queryCount, destination, destinationOffset);
}

function _wgpuComputePassEncoderDispatchWorkgroups(passPtr, x, y, z) {
  passPtr = bigintToI53Checked(passPtr);
  assert(x >= 0);
  assert(y >= 0);
  assert(z >= 0);
  var pass = WebGPU.getJsObject(passPtr);
  pass.dispatchWorkgroups(x, y, z);
}

function _wgpuComputePassEncoderDispatchWorkgroupsIndirect(passPtr, indirectBufferPtr, indirectOffset) {
  passPtr = bigintToI53Checked(passPtr);
  indirectBufferPtr = bigintToI53Checked(indirectBufferPtr);
  indirectOffset = bigintToI53Checked(indirectOffset);
  var indirectBuffer = WebGPU.getJsObject(indirectBufferPtr);
  var pass = WebGPU.getJsObject(passPtr);
  pass.dispatchWorkgroupsIndirect(indirectBuffer, indirectOffset);
}

function _wgpuComputePassEncoderEnd(passPtr) {
  passPtr = bigintToI53Checked(passPtr);
  var pass = WebGPU.getJsObject(passPtr);
  pass.end();
}

function _wgpuComputePassEncoderSetBindGroup(passPtr, groupIndex, groupPtr, dynamicOffsetCount, dynamicOffsetsPtr) {
  passPtr = bigintToI53Checked(passPtr);
  groupPtr = bigintToI53Checked(groupPtr);
  dynamicOffsetCount = bigintToI53Checked(dynamicOffsetCount);
  dynamicOffsetsPtr = bigintToI53Checked(dynamicOffsetsPtr);
  assert(groupIndex >= 0);
  var pass = WebGPU.getJsObject(passPtr);
  var group = WebGPU.getJsObject(groupPtr);
  if (dynamicOffsetCount == 0) {
    pass.setBindGroup(groupIndex, group);
  } else {
    pass.setBindGroup(groupIndex, group, (growMemViews(), HEAPU32), ((dynamicOffsetsPtr) >> 2), dynamicOffsetCount);
  }
}

function _wgpuComputePassEncoderSetPipeline(passPtr, pipelinePtr) {
  passPtr = bigintToI53Checked(passPtr);
  pipelinePtr = bigintToI53Checked(pipelinePtr);
  var pass = WebGPU.getJsObject(passPtr);
  var pipeline = WebGPU.getJsObject(pipelinePtr);
  pass.setPipeline(pipeline);
}

var _wgpuComputePipelineGetBindGroupLayout = function(pipelinePtr, groupIndex) {
  pipelinePtr = bigintToI53Checked(pipelinePtr);
  var ret = (() => {
    assert(groupIndex >= 0);
    var pipeline = WebGPU.getJsObject(pipelinePtr);
    var ptr = _emwgpuCreateBindGroupLayout(0n);
    WebGPU.Internals.jsObjectInsert(ptr, pipeline.getBindGroupLayout(groupIndex));
    return ptr;
  })();
  return BigInt(ret);
};

var _wgpuDeviceCreateBindGroup = function(devicePtr, descriptor) {
  devicePtr = bigintToI53Checked(devicePtr);
  descriptor = bigintToI53Checked(descriptor);
  var ret = (() => {
    assert(descriptor);
    assert(Number((growMemViews(), HEAPU64)[((descriptor) >> 3)]) === 0);
    function makeEntry(entryPtr) {
      assert(entryPtr);
      var bufferPtr = Number((growMemViews(), HEAPU64)[(((entryPtr) + (16)) >> 3)]);
      var samplerPtr = Number((growMemViews(), HEAPU64)[(((entryPtr) + (40)) >> 3)]);
      var textureViewPtr = Number((growMemViews(), HEAPU64)[(((entryPtr) + (48)) >> 3)]);
      var externalTexturePtr = 0;
      WebGPU.iterateExtensions(entryPtr, {
        14: ptr => {
          externalTexturePtr = Number((growMemViews(), HEAPU64)[(((ptr) + (16)) >> 3)]);
        }
      });
      assert((bufferPtr !== 0) + (samplerPtr !== 0) + (textureViewPtr !== 0) + (externalTexturePtr !== 0) === 1);
      var resource;
      if (bufferPtr) {
        // Note the sentinel UINT64_MAX will be read as -1.
        var size = readI53FromI64((entryPtr) + (32));
        if (size == -1) size = undefined;
        resource = {
          "buffer": WebGPU.getJsObject(bufferPtr),
          "offset": readI53FromI64((entryPtr) + (24)),
          "size": size
        };
      } else {
        resource = WebGPU.getJsObject(samplerPtr || textureViewPtr || externalTexturePtr);
      }
      return {
        "binding": (growMemViews(), HEAPU32)[(((entryPtr) + (8)) >> 2)],
        "resource": resource
      };
    }
    function makeEntries(count, entriesPtrs) {
      var entries = [];
      for (var i = 0; i < count; ++i) {
        entries.push(makeEntry(entriesPtrs + 56 * i));
      }
      return entries;
    }
    var desc = {
      "label": WebGPU.makeStringFromOptionalStringView(descriptor + 8),
      "layout": WebGPU.getJsObject(Number((growMemViews(), HEAPU64)[(((descriptor) + (24)) >> 3)])),
      "entries": makeEntries(Number((growMemViews(), HEAPU64)[(((descriptor) + (32)) >> 3)]), Number((growMemViews(), 
      HEAPU64)[(((descriptor) + (40)) >> 3)]))
    };
    var device = WebGPU.getJsObject(devicePtr);
    var ptr = _emwgpuCreateBindGroup(0n);
    WebGPU.Internals.jsObjectInsert(ptr, device.createBindGroup(desc));
    return ptr;
  })();
  return BigInt(ret);
};

var _wgpuDeviceCreateBindGroupLayout = function(devicePtr, descriptor) {
  devicePtr = bigintToI53Checked(devicePtr);
  descriptor = bigintToI53Checked(descriptor);
  var ret = (() => {
    assert(descriptor);
    assert(Number((growMemViews(), HEAPU64)[((descriptor) >> 3)]) === 0);
    function makeBufferEntry(substructPtr) {
      var typeInt = (growMemViews(), HEAPU32)[(((substructPtr) + (8)) >> 2)];
      if (!typeInt) return undefined;
      return {
        "type": WebGPU.BufferBindingType[typeInt],
        "hasDynamicOffset": !!((growMemViews(), HEAPU32)[(((substructPtr) + (12)) >> 2)]),
        "minBindingSize": readI53FromI64((substructPtr) + (16))
      };
    }
    function makeSamplerEntry(substructPtr) {
      var typeInt = (growMemViews(), HEAPU32)[(((substructPtr) + (8)) >> 2)];
      if (!typeInt) return undefined;
      return {
        "type": WebGPU.SamplerBindingType[typeInt]
      };
    }
    function makeTextureEntry(substructPtr) {
      var sampleTypeInt = (growMemViews(), HEAPU32)[(((substructPtr) + (8)) >> 2)];
      if (!sampleTypeInt) return undefined;
      return {
        "sampleType": WebGPU.TextureSampleType[sampleTypeInt],
        "viewDimension": WebGPU.TextureViewDimension[(growMemViews(), HEAP32)[(((substructPtr) + (12)) >> 2)]],
        "multisampled": !!((growMemViews(), HEAPU32)[(((substructPtr) + (16)) >> 2)])
      };
    }
    function makeStorageTextureEntry(substructPtr) {
      var accessInt = (growMemViews(), HEAPU32)[(((substructPtr) + (8)) >> 2)];
      if (!accessInt) return undefined;
      return {
        "access": WebGPU.StorageTextureAccess[accessInt],
        "format": WebGPU.TextureFormat[(growMemViews(), HEAP32)[(((substructPtr) + (12)) >> 2)]],
        "viewDimension": WebGPU.TextureViewDimension[(growMemViews(), HEAP32)[(((substructPtr) + (16)) >> 2)]]
      };
    }
    function makeEntry(entryPtr) {
      assert(entryPtr);
      // bindingArraySize is not specced and thus not implemented yet. We don't pass it through
      // because if we did, then existing apps using this version of the bindings could break when
      // browsers start accepting bindingArraySize.
      var bindingArraySize = (growMemViews(), HEAPU32)[(((entryPtr) + (24)) >> 2)];
      assert(bindingArraySize == 0 || bindingArraySize == 1);
      var entry = {
        "binding": (growMemViews(), HEAPU32)[(((entryPtr) + (8)) >> 2)],
        "visibility": (growMemViews(), HEAPU32)[(((entryPtr) + (16)) >> 2)],
        "buffer": makeBufferEntry(entryPtr + 32),
        "sampler": makeSamplerEntry(entryPtr + 56),
        "texture": makeTextureEntry(entryPtr + 72),
        "storageTexture": makeStorageTextureEntry(entryPtr + 96)
      };
      WebGPU.iterateExtensions(entryPtr, {
        13: ptr => {
          entry["externalTexture"] = {};
        }
      });
      return entry;
    }
    function makeEntries(count, entriesPtrs) {
      var entries = [];
      for (var i = 0; i < count; ++i) {
        entries.push(makeEntry(entriesPtrs + 120 * i));
      }
      return entries;
    }
    var desc = {
      "label": WebGPU.makeStringFromOptionalStringView(descriptor + 8),
      "entries": makeEntries(Number((growMemViews(), HEAPU64)[(((descriptor) + (24)) >> 3)]), Number((growMemViews(), 
      HEAPU64)[(((descriptor) + (32)) >> 3)]))
    };
    var device = WebGPU.getJsObject(devicePtr);
    var ptr = _emwgpuCreateBindGroupLayout(0n);
    WebGPU.Internals.jsObjectInsert(ptr, device.createBindGroupLayout(desc));
    return ptr;
  })();
  return BigInt(ret);
};

var _wgpuDeviceCreateCommandEncoder = function(devicePtr, descriptor) {
  devicePtr = bigintToI53Checked(devicePtr);
  descriptor = bigintToI53Checked(descriptor);
  var ret = (() => {
    var desc;
    if (descriptor) {
      assert(descriptor);
      assert(Number((growMemViews(), HEAPU64)[((descriptor) >> 3)]) === 0);
      desc = {
        "label": WebGPU.makeStringFromOptionalStringView(descriptor + 8)
      };
    }
    var device = WebGPU.getJsObject(devicePtr);
    var ptr = _emwgpuCreateCommandEncoder(0n);
    WebGPU.Internals.jsObjectInsert(ptr, device.createCommandEncoder(desc));
    return ptr;
  })();
  return BigInt(ret);
};

var _wgpuDeviceCreateComputePipeline = function(devicePtr, descriptor) {
  devicePtr = bigintToI53Checked(devicePtr);
  descriptor = bigintToI53Checked(descriptor);
  var ret = (() => {
    var desc = WebGPU.makeComputePipelineDesc(descriptor);
    var device = WebGPU.getJsObject(devicePtr);
    var ptr = _emwgpuCreateComputePipeline(0n);
    WebGPU.Internals.jsObjectInsert(ptr, device.createComputePipeline(desc));
    return ptr;
  })();
  return BigInt(ret);
};

var _wgpuDeviceCreatePipelineLayout = function(devicePtr, descriptor) {
  devicePtr = bigintToI53Checked(devicePtr);
  descriptor = bigintToI53Checked(descriptor);
  var ret = (() => {
    assert(descriptor);
    assert(Number((growMemViews(), HEAPU64)[((descriptor) >> 3)]) === 0);
    var bglCount = Number((growMemViews(), HEAPU64)[(((descriptor) + (24)) >> 3)]);
    var bglPtr = Number((growMemViews(), HEAPU64)[(((descriptor) + (32)) >> 3)]);
    var bgls = [];
    for (var i = 0; i < bglCount; ++i) {
      bgls.push(WebGPU.getJsObject(Number((growMemViews(), HEAPU64)[(((bglPtr) + (8 * i)) >> 3)])));
    }
    var desc = {
      "label": WebGPU.makeStringFromOptionalStringView(descriptor + 8),
      "bindGroupLayouts": bgls,
      "immediateSize": (growMemViews(), HEAPU32)[(((descriptor) + (40)) >> 2)]
    };
    var device = WebGPU.getJsObject(devicePtr);
    var ptr = _emwgpuCreatePipelineLayout(0n);
    WebGPU.Internals.jsObjectInsert(ptr, device.createPipelineLayout(desc));
    return ptr;
  })();
  return BigInt(ret);
};

var _wgpuDeviceCreateQuerySet = function(devicePtr, descriptor) {
  devicePtr = bigintToI53Checked(devicePtr);
  descriptor = bigintToI53Checked(descriptor);
  var ret = (() => {
    assert(descriptor);
    assert(Number((growMemViews(), HEAPU64)[((descriptor) >> 3)]) === 0);
    var desc = {
      "type": WebGPU.QueryType[(growMemViews(), HEAP32)[(((descriptor) + (24)) >> 2)]],
      "count": (growMemViews(), HEAPU32)[(((descriptor) + (28)) >> 2)]
    };
    var device = WebGPU.getJsObject(devicePtr);
    var ptr = _emwgpuCreateQuerySet(0n);
    WebGPU.Internals.jsObjectInsert(ptr, device.createQuerySet(desc));
    return ptr;
  })();
  return BigInt(ret);
};

var _wgpuDeviceCreateRenderBundleEncoder = function(devicePtr, descriptor) {
  devicePtr = bigintToI53Checked(devicePtr);
  descriptor = bigintToI53Checked(descriptor);
  var ret = (() => {
    assert(descriptor);
    function makeRenderBundleEncoderDescriptor(descriptor) {
      assert(descriptor);
      function makeColorFormats(count, formatsPtr) {
        var formats = [];
        for (var i = 0; i < count; ++i, formatsPtr += 4) {
          // format == undefined means no attachment in this slot.
          formats.push(WebGPU.TextureFormat[(growMemViews(), HEAP32)[((formatsPtr) >> 2)]]);
        }
        return formats;
      }
      var desc = {
        "label": WebGPU.makeStringFromOptionalStringView(descriptor + 8),
        "colorFormats": makeColorFormats(Number((growMemViews(), HEAPU64)[(((descriptor) + (24)) >> 3)]), Number((growMemViews(), 
        HEAPU64)[(((descriptor) + (32)) >> 3)])),
        "depthStencilFormat": WebGPU.TextureFormat[(growMemViews(), HEAP32)[(((descriptor) + (40)) >> 2)]],
        "sampleCount": (growMemViews(), HEAPU32)[(((descriptor) + (44)) >> 2)],
        "depthReadOnly": !!((growMemViews(), HEAPU32)[(((descriptor) + (48)) >> 2)]),
        "stencilReadOnly": !!((growMemViews(), HEAPU32)[(((descriptor) + (52)) >> 2)])
      };
      return desc;
    }
    var desc = makeRenderBundleEncoderDescriptor(descriptor);
    var device = WebGPU.getJsObject(devicePtr);
    var ptr = _emwgpuCreateRenderBundleEncoder(0n);
    WebGPU.Internals.jsObjectInsert(ptr, device.createRenderBundleEncoder(desc));
    return ptr;
  })();
  return BigInt(ret);
};

var _wgpuDeviceCreateRenderPipeline = function(devicePtr, descriptor) {
  devicePtr = bigintToI53Checked(devicePtr);
  descriptor = bigintToI53Checked(descriptor);
  var ret = (() => {
    var desc = WebGPU.makeRenderPipelineDesc(descriptor);
    var device = WebGPU.getJsObject(devicePtr);
    var ptr = _emwgpuCreateRenderPipeline(0n);
    WebGPU.Internals.jsObjectInsert(ptr, device.createRenderPipeline(desc));
    return ptr;
  })();
  return BigInt(ret);
};

/** @type {!Uint16Array} */ var HEAPU16;

var _wgpuDeviceCreateSampler = function(devicePtr, descriptor) {
  devicePtr = bigintToI53Checked(devicePtr);
  descriptor = bigintToI53Checked(descriptor);
  var ret = (() => {
    var desc;
    if (descriptor) {
      assert(descriptor);
      assert(Number((growMemViews(), HEAPU64)[((descriptor) >> 3)]) === 0);
      desc = {
        "label": WebGPU.makeStringFromOptionalStringView(descriptor + 8),
        "addressModeU": WebGPU.AddressMode[(growMemViews(), HEAP32)[(((descriptor) + (24)) >> 2)]],
        "addressModeV": WebGPU.AddressMode[(growMemViews(), HEAP32)[(((descriptor) + (28)) >> 2)]],
        "addressModeW": WebGPU.AddressMode[(growMemViews(), HEAP32)[(((descriptor) + (32)) >> 2)]],
        "magFilter": WebGPU.FilterMode[(growMemViews(), HEAP32)[(((descriptor) + (36)) >> 2)]],
        "minFilter": WebGPU.FilterMode[(growMemViews(), HEAP32)[(((descriptor) + (40)) >> 2)]],
        "mipmapFilter": WebGPU.MipmapFilterMode[(growMemViews(), HEAP32)[(((descriptor) + (44)) >> 2)]],
        "lodMinClamp": (growMemViews(), HEAPF32)[(((descriptor) + (48)) >> 2)],
        "lodMaxClamp": (growMemViews(), HEAPF32)[(((descriptor) + (52)) >> 2)],
        "compare": WebGPU.CompareFunction[(growMemViews(), HEAP32)[(((descriptor) + (56)) >> 2)]],
        "maxAnisotropy": (growMemViews(), HEAPU16)[(((descriptor) + (60)) >> 1)]
      };
    }
    var device = WebGPU.getJsObject(devicePtr);
    var ptr = _emwgpuCreateSampler(0n);
    WebGPU.Internals.jsObjectInsert(ptr, device.createSampler(desc));
    return ptr;
  })();
  return BigInt(ret);
};

var _wgpuDeviceCreateTexture = function(devicePtr, descriptor) {
  devicePtr = bigintToI53Checked(devicePtr);
  descriptor = bigintToI53Checked(descriptor);
  var ret = (() => {
    assert(descriptor);
    var nextInChainPtr = Number((growMemViews(), HEAPU64)[((descriptor) >> 3)]);
    var textureBindingViewDimension;
    if (nextInChainPtr !== 0) {
      var sType = (growMemViews(), HEAP32)[(((nextInChainPtr) + (8)) >> 2)];
      assert(sType === 16);
      assert(0 === Number((growMemViews(), HEAPU64)[((nextInChainPtr) >> 3)]));
      var textureBindingViewDimensionDescriptor = nextInChainPtr;
      assert(textureBindingViewDimensionDescriptor);
      assert(Number((growMemViews(), HEAPU64)[((textureBindingViewDimensionDescriptor) >> 3)]) === 0);
      textureBindingViewDimension = WebGPU.TextureViewDimension[(growMemViews(), HEAP32)[(((textureBindingViewDimensionDescriptor) + (16)) >> 2)]];
    }
    var desc = {
      "label": WebGPU.makeStringFromOptionalStringView(descriptor + 8),
      "size": WebGPU.makeExtent3D(descriptor + 36),
      "mipLevelCount": (growMemViews(), HEAPU32)[(((descriptor) + (52)) >> 2)],
      "sampleCount": (growMemViews(), HEAPU32)[(((descriptor) + (56)) >> 2)],
      "dimension": WebGPU.TextureDimension[(growMemViews(), HEAP32)[(((descriptor) + (32)) >> 2)]],
      "format": WebGPU.TextureFormat[(growMemViews(), HEAP32)[(((descriptor) + (48)) >> 2)]],
      "usage": (growMemViews(), HEAPU32)[(((descriptor) + (24)) >> 2)],
      "textureBindingViewDimension": textureBindingViewDimension
    };
    var viewFormatCount = Number((growMemViews(), HEAPU64)[(((descriptor) + (64)) >> 3)]);
    if (viewFormatCount) {
      var viewFormatsPtr = Number((growMemViews(), HEAPU64)[(((descriptor) + (72)) >> 3)]);
      // viewFormatsPtr pointer to an array of TextureFormat which is an enum of size uint32_t
      desc["viewFormats"] = Array.from((growMemViews(), HEAP32).subarray((((viewFormatsPtr) >> 2)), ((viewFormatsPtr + viewFormatCount * 4) >> 2)), format => WebGPU.TextureFormat[format]);
    }
    var device = WebGPU.getJsObject(devicePtr);
    var ptr = _emwgpuCreateTexture(0n);
    WebGPU.Internals.jsObjectInsert(ptr, device.createTexture(desc));
    return ptr;
  })();
  return BigInt(ret);
};

function _wgpuDeviceGetFeatures(devicePtr, supportedFeatures) {
  devicePtr = bigintToI53Checked(devicePtr);
  supportedFeatures = bigintToI53Checked(supportedFeatures);
  var device = WebGPU.getJsObject(devicePtr);
  // Always allocate enough space for all the features, though some may be unused.
  var featuresPtr = _malloc(device.features.size * 4);
  var offset = 0;
  var numFeatures = 0;
  for (const feature of device.features) {
    var featureEnumValue = emwgpuStringToInt_FeatureName[feature];
    if (featureEnumValue >= 0) {
      (growMemViews(), HEAP32)[(((featuresPtr) + (offset)) >> 2)] = featureEnumValue;
      offset += 4;
      numFeatures++;
    }
  }
  (growMemViews(), HEAPU64)[(((supportedFeatures) + (8)) >> 3)] = BigInt(featuresPtr);
  (growMemViews(), HEAPU64)[((supportedFeatures) >> 3)] = BigInt(numFeatures);
}

function _wgpuDeviceGetLimits(devicePtr, limitsOutPtr) {
  devicePtr = bigintToI53Checked(devicePtr);
  limitsOutPtr = bigintToI53Checked(limitsOutPtr);
  var device = WebGPU.getJsObject(devicePtr);
  WebGPU.fillLimitStruct(device.limits, limitsOutPtr);
  return 1;
}

function _wgpuDeviceHasFeature(devicePtr, featureEnumValue) {
  devicePtr = bigintToI53Checked(devicePtr);
  var device = WebGPU.getJsObject(devicePtr);
  return device.features.has(WebGPU.FeatureName[featureEnumValue]);
}

function _wgpuDevicePushErrorScope(devicePtr, filter) {
  devicePtr = bigintToI53Checked(devicePtr);
  var device = WebGPU.getJsObject(devicePtr);
  device.pushErrorScope(WebGPU.ErrorFilter[filter]);
}

var findCanvasEventTarget = findEventTarget;

var _wgpuInstanceCreateSurface = function(instancePtr, descriptor) {
  instancePtr = bigintToI53Checked(instancePtr);
  descriptor = bigintToI53Checked(descriptor);
  var ret = (() => {
    assert(descriptor);
    var nextInChainPtr = Number((growMemViews(), HEAPU64)[((descriptor) >> 3)]);
    assert(nextInChainPtr !== 0);
    assert(262144 === (growMemViews(), HEAP32)[(((nextInChainPtr) + (8)) >> 2)]);
    var sourceCanvasHTMLSelector = nextInChainPtr;
    assert(sourceCanvasHTMLSelector);
    assert(Number((growMemViews(), HEAPU64)[((sourceCanvasHTMLSelector) >> 3)]) === 0);
    var selectorPtr = Number((growMemViews(), HEAPU64)[(((sourceCanvasHTMLSelector) + (16)) >> 3)]);
    assert(selectorPtr);
    var canvas = findCanvasEventTarget(selectorPtr);
    var context = canvas.getContext("webgpu");
    assert(context);
    if (!context) return 0n;
    context.surfaceLabelWebGPU = WebGPU.makeStringFromOptionalStringView(descriptor + 8);
    var ptr = _emwgpuCreateSurface(0n);
    WebGPU.Internals.jsObjectInsert(ptr, context);
    return ptr;
  })();
  return BigInt(ret);
};

var _wgpuQueueSubmit = function(queuePtr, commandCount, commands) {
  queuePtr = bigintToI53Checked(queuePtr);
  commandCount = bigintToI53Checked(commandCount);
  commands = bigintToI53Checked(commands);
  assert(commands % 4 === 0);
  var queue = WebGPU.getJsObject(queuePtr);
  var cmds = Array.from((growMemViews(), HEAP64).subarray((((commands) >> 3)), ((commands + commandCount * 8) >> 3)), id => WebGPU.getJsObject(id));
  queue.submit(cmds);
};

function _wgpuQueueWriteBuffer(queuePtr, bufferPtr, bufferOffset, data, size) {
  queuePtr = bigintToI53Checked(queuePtr);
  bufferPtr = bigintToI53Checked(bufferPtr);
  bufferOffset = bigintToI53Checked(bufferOffset);
  data = bigintToI53Checked(data);
  size = bigintToI53Checked(size);
  var queue = WebGPU.getJsObject(queuePtr);
  var buffer = WebGPU.getJsObject(bufferPtr);
  // There is a size limitation for ArrayBufferView. Work around by passing in a subarray
  // instead of the whole heap. crbug.com/1201109
  var subarray = (growMemViews(), HEAPU8).subarray(data, data + size);
  queue.writeBuffer(buffer, bufferOffset, subarray, 0, size);
}

function _wgpuQueueWriteTexture(queuePtr, destinationPtr, data, dataSize, dataLayoutPtr, writeSizePtr) {
  queuePtr = bigintToI53Checked(queuePtr);
  destinationPtr = bigintToI53Checked(destinationPtr);
  data = bigintToI53Checked(data);
  dataSize = bigintToI53Checked(dataSize);
  dataLayoutPtr = bigintToI53Checked(dataLayoutPtr);
  writeSizePtr = bigintToI53Checked(writeSizePtr);
  var queue = WebGPU.getJsObject(queuePtr);
  var destination = WebGPU.makeTexelCopyTextureInfo(destinationPtr);
  var dataLayout = WebGPU.makeTexelCopyBufferLayout(dataLayoutPtr);
  var writeSize = WebGPU.makeExtent3D(writeSizePtr);
  // This subarray isn't strictly necessary, but helps work around an issue
  // where Chromium makes a copy of the entire heap. crbug.com/1134457
  var subarray = (growMemViews(), HEAPU8).subarray(data, data + dataSize);
  queue.writeTexture(destination, subarray, dataLayout, writeSize);
}

function _wgpuRenderBundleEncoderDraw(passPtr, vertexCount, instanceCount, firstVertex, firstInstance) {
  passPtr = bigintToI53Checked(passPtr);
  assert(vertexCount >= 0);
  assert(instanceCount >= 0);
  firstVertex >>>= 0;
  firstInstance >>>= 0;
  var pass = WebGPU.getJsObject(passPtr);
  pass.draw(vertexCount, instanceCount, firstVertex, firstInstance);
}

function _wgpuRenderBundleEncoderDrawIndexed(passPtr, indexCount, instanceCount, firstIndex, baseVertex, firstInstance) {
  passPtr = bigintToI53Checked(passPtr);
  assert(indexCount >= 0);
  assert(instanceCount >= 0);
  firstIndex >>>= 0;
  firstInstance >>>= 0;
  var pass = WebGPU.getJsObject(passPtr);
  pass.drawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
}

var _wgpuRenderBundleEncoderFinish = function(encoderPtr, descriptor) {
  encoderPtr = bigintToI53Checked(encoderPtr);
  descriptor = bigintToI53Checked(descriptor);
  var ret = (() => {
    var desc;
    if (descriptor) {
      assert(descriptor);
      assert(Number((growMemViews(), HEAPU64)[((descriptor) >> 3)]) === 0);
      desc = {
        "label": WebGPU.makeStringFromOptionalStringView(descriptor + 8)
      };
    }
    var encoder = WebGPU.getJsObject(encoderPtr);
    var ptr = _emwgpuCreateRenderBundle(0n);
    WebGPU.Internals.jsObjectInsert(ptr, encoder.finish(desc));
    return ptr;
  })();
  return BigInt(ret);
};

function _wgpuRenderBundleEncoderSetBindGroup(passPtr, groupIndex, groupPtr, dynamicOffsetCount, dynamicOffsetsPtr) {
  passPtr = bigintToI53Checked(passPtr);
  groupPtr = bigintToI53Checked(groupPtr);
  dynamicOffsetCount = bigintToI53Checked(dynamicOffsetCount);
  dynamicOffsetsPtr = bigintToI53Checked(dynamicOffsetsPtr);
  assert(groupIndex >= 0);
  var pass = WebGPU.getJsObject(passPtr);
  var group = WebGPU.getJsObject(groupPtr);
  if (dynamicOffsetCount == 0) {
    pass.setBindGroup(groupIndex, group);
  } else {
    pass.setBindGroup(groupIndex, group, (growMemViews(), HEAPU32), ((dynamicOffsetsPtr) >> 2), dynamicOffsetCount);
  }
}

function _wgpuRenderBundleEncoderSetIndexBuffer(passPtr, bufferPtr, format, offset, size) {
  passPtr = bigintToI53Checked(passPtr);
  bufferPtr = bigintToI53Checked(bufferPtr);
  offset = bigintToI53Checked(offset);
  size = bigintToI53Checked(size);
  var pass = WebGPU.getJsObject(passPtr);
  var buffer = WebGPU.getJsObject(bufferPtr);
  if (size == -1) size = undefined;
  pass.setIndexBuffer(buffer, WebGPU.IndexFormat[format], offset, size);
}

function _wgpuRenderBundleEncoderSetPipeline(passPtr, pipelinePtr) {
  passPtr = bigintToI53Checked(passPtr);
  pipelinePtr = bigintToI53Checked(pipelinePtr);
  var pass = WebGPU.getJsObject(passPtr);
  var pipeline = WebGPU.getJsObject(pipelinePtr);
  pass.setPipeline(pipeline);
}

function _wgpuRenderBundleEncoderSetVertexBuffer(passPtr, slot, bufferPtr, offset, size) {
  passPtr = bigintToI53Checked(passPtr);
  bufferPtr = bigintToI53Checked(bufferPtr);
  offset = bigintToI53Checked(offset);
  size = bigintToI53Checked(size);
  assert(slot >= 0);
  var pass = WebGPU.getJsObject(passPtr);
  var buffer = WebGPU.getJsObject(bufferPtr);
  if (size == -1) size = undefined;
  pass.setVertexBuffer(slot, buffer, offset, size);
}

function _wgpuRenderPassEncoderBeginOcclusionQuery(passPtr, queryIndex) {
  passPtr = bigintToI53Checked(passPtr);
  assert(queryIndex >= 0);
  var pass = WebGPU.getJsObject(passPtr);
  pass.beginOcclusionQuery(queryIndex);
}

function _wgpuRenderPassEncoderDraw(passPtr, vertexCount, instanceCount, firstVertex, firstInstance) {
  passPtr = bigintToI53Checked(passPtr);
  assert(vertexCount >= 0);
  assert(instanceCount >= 0);
  firstVertex >>>= 0;
  firstInstance >>>= 0;
  var pass = WebGPU.getJsObject(passPtr);
  pass.draw(vertexCount, instanceCount, firstVertex, firstInstance);
}

function _wgpuRenderPassEncoderDrawIndexed(passPtr, indexCount, instanceCount, firstIndex, baseVertex, firstInstance) {
  passPtr = bigintToI53Checked(passPtr);
  assert(indexCount >= 0);
  assert(instanceCount >= 0);
  firstIndex >>>= 0;
  firstInstance >>>= 0;
  var pass = WebGPU.getJsObject(passPtr);
  pass.drawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
}

function _wgpuRenderPassEncoderDrawIndexedIndirect(passPtr, indirectBufferPtr, indirectOffset) {
  passPtr = bigintToI53Checked(passPtr);
  indirectBufferPtr = bigintToI53Checked(indirectBufferPtr);
  indirectOffset = bigintToI53Checked(indirectOffset);
  var pass = WebGPU.getJsObject(passPtr);
  var indirectBuffer = WebGPU.getJsObject(indirectBufferPtr);
  pass.drawIndexedIndirect(indirectBuffer, indirectOffset);
}

function _wgpuRenderPassEncoderDrawIndirect(passPtr, indirectBufferPtr, indirectOffset) {
  passPtr = bigintToI53Checked(passPtr);
  indirectBufferPtr = bigintToI53Checked(indirectBufferPtr);
  indirectOffset = bigintToI53Checked(indirectOffset);
  var pass = WebGPU.getJsObject(passPtr);
  var indirectBuffer = WebGPU.getJsObject(indirectBufferPtr);
  pass.drawIndirect(indirectBuffer, indirectOffset);
}

function _wgpuRenderPassEncoderEnd(encoderPtr) {
  encoderPtr = bigintToI53Checked(encoderPtr);
  var encoder = WebGPU.getJsObject(encoderPtr);
  encoder.end();
}

function _wgpuRenderPassEncoderEndOcclusionQuery(passPtr) {
  passPtr = bigintToI53Checked(passPtr);
  var pass = WebGPU.getJsObject(passPtr);
  pass.endOcclusionQuery();
}

var _wgpuRenderPassEncoderExecuteBundles = function(passPtr, count, bundlesPtr) {
  passPtr = bigintToI53Checked(passPtr);
  count = bigintToI53Checked(count);
  bundlesPtr = bigintToI53Checked(bundlesPtr);
  var pass = WebGPU.getJsObject(passPtr);
  assert(bundlesPtr % 4 === 0);
  var bundles = Array.from((growMemViews(), HEAP64).subarray((((bundlesPtr) >> 3)), ((bundlesPtr + count * 8) >> 3)), id => WebGPU.getJsObject(id));
  pass.executeBundles(bundles);
};

function _wgpuRenderPassEncoderSetBindGroup(passPtr, groupIndex, groupPtr, dynamicOffsetCount, dynamicOffsetsPtr) {
  passPtr = bigintToI53Checked(passPtr);
  groupPtr = bigintToI53Checked(groupPtr);
  dynamicOffsetCount = bigintToI53Checked(dynamicOffsetCount);
  dynamicOffsetsPtr = bigintToI53Checked(dynamicOffsetsPtr);
  assert(groupIndex >= 0);
  var pass = WebGPU.getJsObject(passPtr);
  var group = WebGPU.getJsObject(groupPtr);
  if (dynamicOffsetCount == 0) {
    pass.setBindGroup(groupIndex, group);
  } else {
    pass.setBindGroup(groupIndex, group, (growMemViews(), HEAPU32), ((dynamicOffsetsPtr) >> 2), dynamicOffsetCount);
  }
}

function _wgpuRenderPassEncoderSetBlendConstant(passPtr, colorPtr) {
  passPtr = bigintToI53Checked(passPtr);
  colorPtr = bigintToI53Checked(colorPtr);
  var pass = WebGPU.getJsObject(passPtr);
  var color = WebGPU.makeColor(colorPtr);
  pass.setBlendConstant(color);
}

function _wgpuRenderPassEncoderSetIndexBuffer(passPtr, bufferPtr, format, offset, size) {
  passPtr = bigintToI53Checked(passPtr);
  bufferPtr = bigintToI53Checked(bufferPtr);
  offset = bigintToI53Checked(offset);
  size = bigintToI53Checked(size);
  var pass = WebGPU.getJsObject(passPtr);
  var buffer = WebGPU.getJsObject(bufferPtr);
  if (size == -1) size = undefined;
  pass.setIndexBuffer(buffer, WebGPU.IndexFormat[format], offset, size);
}

function _wgpuRenderPassEncoderSetPipeline(passPtr, pipelinePtr) {
  passPtr = bigintToI53Checked(passPtr);
  pipelinePtr = bigintToI53Checked(pipelinePtr);
  var pass = WebGPU.getJsObject(passPtr);
  var pipeline = WebGPU.getJsObject(pipelinePtr);
  pass.setPipeline(pipeline);
}

function _wgpuRenderPassEncoderSetScissorRect(passPtr, x, y, w, h) {
  passPtr = bigintToI53Checked(passPtr);
  assert(x >= 0);
  assert(y >= 0);
  assert(w >= 0);
  assert(h >= 0);
  var pass = WebGPU.getJsObject(passPtr);
  pass.setScissorRect(x, y, w, h);
}

function _wgpuRenderPassEncoderSetStencilReference(passPtr, reference) {
  passPtr = bigintToI53Checked(passPtr);
  reference >>>= 0;
  var pass = WebGPU.getJsObject(passPtr);
  pass.setStencilReference(reference);
}

function _wgpuRenderPassEncoderSetVertexBuffer(passPtr, slot, bufferPtr, offset, size) {
  passPtr = bigintToI53Checked(passPtr);
  bufferPtr = bigintToI53Checked(bufferPtr);
  offset = bigintToI53Checked(offset);
  size = bigintToI53Checked(size);
  assert(slot >= 0);
  var pass = WebGPU.getJsObject(passPtr);
  var buffer = WebGPU.getJsObject(bufferPtr);
  if (size == -1) size = undefined;
  pass.setVertexBuffer(slot, buffer, offset, size);
}

function _wgpuRenderPassEncoderSetViewport(passPtr, x, y, w, h, minDepth, maxDepth) {
  passPtr = bigintToI53Checked(passPtr);
  var pass = WebGPU.getJsObject(passPtr);
  pass.setViewport(x, y, w, h, minDepth, maxDepth);
}

function _wgpuSurfaceConfigure(surfacePtr, config) {
  surfacePtr = bigintToI53Checked(surfacePtr);
  config = bigintToI53Checked(config);
  assert(config);
  var context = WebGPU.getJsObject(surfacePtr);
  var presentMode = (growMemViews(), HEAPU32)[(((config) + (60)) >> 2)];
  assert(presentMode === 1 || presentMode === 0);
  var canvasSize = [ (growMemViews(), HEAPU32)[(((config) + (32)) >> 2)], (growMemViews(), 
  HEAPU32)[(((config) + (36)) >> 2)] ];
  if (canvasSize[0] !== 0) {
    context["canvas"]["width"] = canvasSize[0];
  }
  if (canvasSize[1] !== 0) {
    context["canvas"]["height"] = canvasSize[1];
  }
  var configuration = {
    "device": WebGPU.getJsObject(Number((growMemViews(), HEAPU64)[(((config) + (8)) >> 3)])),
    "format": WebGPU.TextureFormat[(growMemViews(), HEAP32)[(((config) + (16)) >> 2)]],
    "usage": (growMemViews(), HEAPU32)[(((config) + (24)) >> 2)],
    "alphaMode": WebGPU.CompositeAlphaMode[(growMemViews(), HEAP32)[(((config) + (56)) >> 2)]]
  };
  var viewFormatCount = Number((growMemViews(), HEAPU64)[(((config) + (40)) >> 3)]);
  if (viewFormatCount) {
    var viewFormatsPtr = Number((growMemViews(), HEAPU64)[(((config) + (48)) >> 3)]);
    // viewFormatsPtr pointer to an array of TextureFormat which is an enum of size uint32_t
    configuration["viewFormats"] = Array.from((growMemViews(), HEAP32).subarray((((viewFormatsPtr) >> 2)), ((viewFormatsPtr + viewFormatCount * 4) >> 2)), format => WebGPU.TextureFormat[format]);
  }
  {
    var nextInChainPtr = Number((growMemViews(), HEAPU64)[((config) >> 3)]);
    if (nextInChainPtr !== 0) {
      var sType = (growMemViews(), HEAP32)[(((nextInChainPtr) + (8)) >> 2)];
      assert(sType === 10);
      assert(0 === Number((growMemViews(), HEAPU64)[((nextInChainPtr) >> 3)]));
      var surfaceColorManagement = nextInChainPtr;
      assert(surfaceColorManagement);
      assert(Number((growMemViews(), HEAPU64)[((surfaceColorManagement) >> 3)]) === 0);
      configuration.colorSpace = WebGPU.PredefinedColorSpace[(growMemViews(), HEAP32)[(((surfaceColorManagement) + (16)) >> 2)]];
      configuration.toneMapping = {
        mode: WebGPU.ToneMappingMode[(growMemViews(), HEAP32)[(((surfaceColorManagement) + (20)) >> 2)]]
      };
    }
  }
  context.configure(configuration);
}

function _wgpuSurfaceGetCurrentTexture(surfacePtr, surfaceTexturePtr) {
  surfacePtr = bigintToI53Checked(surfacePtr);
  surfaceTexturePtr = bigintToI53Checked(surfaceTexturePtr);
  assert(surfaceTexturePtr);
  var context = WebGPU.getJsObject(surfacePtr);
  try {
    var texturePtr = _emwgpuCreateTexture(0n);
    WebGPU.Internals.jsObjectInsert(texturePtr, context.getCurrentTexture());
    (growMemViews(), HEAPU64)[(((surfaceTexturePtr) + (8)) >> 3)] = BigInt(texturePtr);
    (growMemViews(), HEAP32)[(((surfaceTexturePtr) + (16)) >> 2)] = 1;
  } catch (ex) {
    err(`wgpuSurfaceGetCurrentTexture() failed: ${ex}`);
    (growMemViews(), HEAPU64)[(((surfaceTexturePtr) + (8)) >> 3)] = 0n;
    (growMemViews(), HEAP32)[(((surfaceTexturePtr) + (16)) >> 2)] = 6;
  }
}

function _wgpuSurfacePresent(surfacePtr) {
  surfacePtr = bigintToI53Checked(surfacePtr);
  // TODO: This could probably be emulated with ASYNCIFY.
  abort("wgpuSurfacePresent is unsupported (use requestAnimationFrame via wasm.h instead)");
}

var _wgpuTextureCreateView = function(texturePtr, descriptor) {
  texturePtr = bigintToI53Checked(texturePtr);
  descriptor = bigintToI53Checked(descriptor);
  var ret = (() => {
    var desc;
    if (descriptor) {
      var swizzle;
      var nextInChainPtr = Number((growMemViews(), HEAPU64)[((descriptor) >> 3)]);
      if (nextInChainPtr !== 0) {
        var sType = (growMemViews(), HEAP32)[(((nextInChainPtr) + (8)) >> 2)];
        assert(sType === 12);
        assert(0 === Number((growMemViews(), HEAPU64)[((nextInChainPtr) >> 3)]));
        var swizzleDescriptor = nextInChainPtr;
        assert(swizzleDescriptor);
        assert(Number((growMemViews(), HEAPU64)[((swizzleDescriptor) >> 3)]) === 0);
        var swizzlePtr = swizzleDescriptor + 16;
        var r = WebGPU.ComponentSwizzle[(growMemViews(), HEAP32)[((swizzlePtr) >> 2)]] || "r";
        var g = WebGPU.ComponentSwizzle[(growMemViews(), HEAP32)[(((swizzlePtr) + (4)) >> 2)]] || "g";
        var b = WebGPU.ComponentSwizzle[(growMemViews(), HEAP32)[(((swizzlePtr) + (8)) >> 2)]] || "b";
        var a = WebGPU.ComponentSwizzle[(growMemViews(), HEAP32)[(((swizzlePtr) + (12)) >> 2)]] || "a";
        swizzle = `${r}${g}${b}${a}`;
      }
      var mipLevelCount = (growMemViews(), HEAPU32)[(((descriptor) + (36)) >> 2)];
      var arrayLayerCount = (growMemViews(), HEAPU32)[(((descriptor) + (44)) >> 2)];
      desc = {
        "label": WebGPU.makeStringFromOptionalStringView(descriptor + 8),
        "format": WebGPU.TextureFormat[(growMemViews(), HEAP32)[(((descriptor) + (24)) >> 2)]],
        "dimension": WebGPU.TextureViewDimension[(growMemViews(), HEAP32)[(((descriptor) + (28)) >> 2)]],
        "baseMipLevel": (growMemViews(), HEAPU32)[(((descriptor) + (32)) >> 2)],
        "mipLevelCount": mipLevelCount === 4294967295 ? undefined : mipLevelCount,
        "baseArrayLayer": (growMemViews(), HEAPU32)[(((descriptor) + (40)) >> 2)],
        "arrayLayerCount": arrayLayerCount === 4294967295 ? undefined : arrayLayerCount,
        "aspect": WebGPU.TextureAspect[(growMemViews(), HEAP32)[(((descriptor) + (48)) >> 2)]],
        "usage": (growMemViews(), HEAPU32)[(((descriptor) + (56)) >> 2)],
        "swizzle": swizzle
      };
    }
    var texture = WebGPU.getJsObject(texturePtr);
    var ptr = _emwgpuCreateTextureView(0n);
    WebGPU.Internals.jsObjectInsert(ptr, texture.createView(desc));
    return ptr;
  })();
  return BigInt(ret);
};

function _wgpuTextureDestroy(texturePtr) {
  texturePtr = bigintToI53Checked(texturePtr);
  WebGPU.getJsObject(texturePtr).destroy();
}

function _wgpuTextureGetHeight(texturePtr) {
  texturePtr = bigintToI53Checked(texturePtr);
  var texture = WebGPU.getJsObject(texturePtr);
  return texture.height;
}

function _wgpuTextureGetWidth(texturePtr) {
  texturePtr = bigintToI53Checked(texturePtr);
  var texture = WebGPU.getJsObject(texturePtr);
  return texture.width;
}

var MEMFS = {
  createBackend(opts) {
    return _wasmfs_create_memory_backend();
  }
};

var PATH = {
  isAbs: path => path.charAt(0) === "/",
  splitPath: filename => {
    var splitPathRe = /^(\/?|)([\s\S]*?)((?:\.{1,2}|[^\/]+?|)(\.[^.\/]*|))(?:[\/]*)$/;
    return splitPathRe.exec(filename).slice(1);
  },
  normalizeArray: (parts, allowAboveRoot) => {
    // if the path tries to go above the root, `up` ends up > 0
    var up = 0;
    for (var i = parts.length - 1; i >= 0; i--) {
      var last = parts[i];
      if (last === ".") {
        parts.splice(i, 1);
      } else if (last === "..") {
        parts.splice(i, 1);
        up++;
      } else if (up) {
        parts.splice(i, 1);
        up--;
      }
    }
    // if the path is allowed to go above the root, restore leading ..s
    if (allowAboveRoot) {
      for (;up; up--) {
        parts.unshift("..");
      }
    }
    return parts;
  },
  normalize: path => {
    var isAbsolute = PATH.isAbs(path), trailingSlash = path.slice(-1) === "/";
    // Normalize the path
    path = PATH.normalizeArray(path.split("/").filter(p => !!p), !isAbsolute).join("/");
    if (!path && !isAbsolute) {
      path = ".";
    }
    if (path && trailingSlash) {
      path += "/";
    }
    return (isAbsolute ? "/" : "") + path;
  },
  dirname: path => {
    var result = PATH.splitPath(path), root = result[0], dir = result[1];
    if (!root && !dir) {
      // No dirname whatsoever
      return ".";
    }
    if (dir) {
      // It has a dirname, strip trailing slash
      dir = dir.slice(0, -1);
    }
    return root + dir;
  },
  basename: path => path && path.match(/([^\/]+|\/)\/*$/)[1],
  join: (...paths) => PATH.normalize(paths.join("/")),
  join2: (l, r) => PATH.normalize(l + "/" + r)
};

var withStackSave = f => {
  var stack = stackSave();
  var ret = f();
  stackRestore(stack);
  return ret;
};

var FS_mknod = (path, mode, dev) => FS.handleError(withStackSave(() => {
  var pathBuffer = stringToUTF8OnStack(path);
  return __wasmfs_mknod(pathBuffer, mode, dev);
}));

var FS_create = (path, mode = 438) => {
  mode &= 4095;
  mode |= 32768;
  return FS_mknod(path, mode, 0);
};

var FS_fileDataToTypedArray = data => {
  if (typeof data == "string") {
    data = intArrayFromString(data, true);
  }
  if (!data.subarray) {
    data = new Uint8Array(data);
  }
  return data;
};

var FS_writeFile = (path, data) => {
  var sp = stackSave();
  var pathBuffer = stringToUTF8OnStack(path);
  data = FS_fileDataToTypedArray(data);
  var len = data.length;
  var dataBuffer = _malloc(len);
  assert(dataBuffer);
  (growMemViews(), HEAPU8).set(data, dataBuffer);
  var ret = __wasmfs_write_file(pathBuffer, dataBuffer, len);
  _free(dataBuffer);
  stackRestore(sp);
  return ret;
};

var FS_createDataFile = (parent, name, fileData, canRead, canWrite, canOwn) => {
  var pathName = name ? parent + "/" + name : parent;
  var mode = FS_getMode(canRead, canWrite);
  if (!wasmFSPreloadingFlushed) {
    // WasmFS code in the wasm is not ready to be called yet. Cache the
    // files we want to create here in JS, and WasmFS will read them
    // later.
    wasmFSPreloadedFiles.push({
      pathName,
      fileData,
      mode
    });
  } else {
    // WasmFS is already running, so create the file normally.
    FS_create(pathName, mode);
    FS_writeFile(pathName, fileData);
  }
};

var asyncLoad = async url => {
  var arrayBuffer = await readAsync(url);
  assert(arrayBuffer, `Loading data file "${url}" failed (no arrayBuffer).`);
  return new Uint8Array(arrayBuffer);
};

var PATH_FS = {
  resolve: (...args) => {
    var resolvedPath = "", resolvedAbsolute = false;
    for (var i = args.length - 1; i >= -1 && !resolvedAbsolute; i--) {
      var path = (i >= 0) ? args[i] : FS.cwd();
      // Skip empty and invalid entries
      if (typeof path != "string") {
        throw new TypeError("Arguments to path.resolve must be strings");
      } else if (!path) {
        return "";
      }
      resolvedPath = path + "/" + resolvedPath;
      resolvedAbsolute = PATH.isAbs(path);
    }
    // At this point the path should be resolved to a full absolute path, but
    // handle relative paths to be safe (might happen when process.cwd() fails)
    resolvedPath = PATH.normalizeArray(resolvedPath.split("/").filter(p => !!p), !resolvedAbsolute).join("/");
    return ((resolvedAbsolute ? "/" : "") + resolvedPath) || ".";
  },
  relative: (from, to) => {
    from = PATH_FS.resolve(from).slice(1);
    to = PATH_FS.resolve(to).slice(1);
    function trim(arr) {
      var start = 0;
      for (;start < arr.length; start++) {
        if (arr[start] !== "") break;
      }
      var end = arr.length - 1;
      for (;end >= 0; end--) {
        if (arr[end] !== "") break;
      }
      if (start > end) return [];
      return arr.slice(start, end - start + 1);
    }
    var fromParts = trim(from.split("/"));
    var toParts = trim(to.split("/"));
    var length = Math.min(fromParts.length, toParts.length);
    var samePartsLength = length;
    for (var i = 0; i < length; i++) {
      if (fromParts[i] !== toParts[i]) {
        samePartsLength = i;
        break;
      }
    }
    var outputParts = [];
    for (var i = samePartsLength; i < fromParts.length; i++) {
      outputParts.push("..");
    }
    outputParts = outputParts.concat(toParts.slice(samePartsLength));
    return outputParts.join("/");
  }
};

var getUniqueRunDependency = id => {
  var orig = id;
  while (1) {
    if (!runDependencyTracking[id]) return id;
    id = orig + Math.random();
  }
};

var preloadPlugins = [];

var FS_handledByPreloadPlugin = async (byteArray, fullname) => {
  // Ensure plugins are ready.
  if (typeof Browser != "undefined") Browser.init();
  for (var plugin of preloadPlugins) {
    if (plugin["canHandle"](fullname)) {
      assert(plugin["handle"].constructor.name === "AsyncFunction", "Filesystem plugin handlers must be async functions (See #24914)");
      return plugin["handle"](byteArray, fullname);
    }
  }
  // If no plugin handled this file then return the original/unmodified
  // byteArray.
  return byteArray;
};

var FS_preloadFile = async (parent, name, url, canRead, canWrite, dontCreateFile, canOwn, preFinish) => {
  // TODO we should allow people to just pass in a complete filename instead
  // of parent and name being that we just join them anyways
  var fullname = name ? PATH_FS.resolve(PATH.join2(parent, name)) : parent;
  var dep = getUniqueRunDependency(`cp ${fullname}`);
  // might have several active requests for the same fullname
  addRunDependency(dep);
  try {
    var byteArray = url;
    if (typeof url == "string") {
      byteArray = await asyncLoad(url);
    }
    byteArray = await FS_handledByPreloadPlugin(byteArray, fullname);
    preFinish?.();
    if (!dontCreateFile) {
      FS_createDataFile(parent, name, byteArray, canRead, canWrite, canOwn);
    }
  } finally {
    removeRunDependency(dep);
  }
};

var FS_createPreloadedFile = (parent, name, url, canRead, canWrite, onload, onerror, dontCreateFile, canOwn, preFinish) => {
  FS_preloadFile(parent, name, url, canRead, canWrite, dontCreateFile, canOwn, preFinish).then(onload).catch(onerror);
};

var FS_getMode = (canRead, canWrite) => {
  var mode = 0;
  if (canRead) mode |= 292 | 73;
  if (canWrite) mode |= 146;
  return mode;
};

var FS_modeStringToFlags = str => {
  if (typeof str != "string") return str;
  var flagModes = {
    "r": 0,
    "r+": 2,
    "w": 512 | 64 | 1,
    "w+": 512 | 64 | 2,
    "a": 1024 | 64 | 1,
    "a+": 1024 | 64 | 2
  };
  var flags = flagModes[str];
  if (typeof flags == "undefined") {
    throw new Error(`Unknown file open mode: ${str}`);
  }
  return flags;
};

var FS_mkdir = (path, mode = 511) => FS.handleError(withStackSave(() => {
  var buffer = stringToUTF8OnStack(path);
  return __wasmfs_mkdir(buffer, mode);
}));

/**
   * @param {number=} mode Optionally, the mode to create in. Uses mkdir's
   *                       default if not set.
   */ var FS_mkdirTree = (path, mode) => {
  var dirs = path.split("/");
  var d = "";
  for (var dir of dirs) {
    if (!dir) continue;
    if (d || PATH.isAbs(path)) d += "/";
    d += dir;
    try {
      FS_mkdir(d, mode);
    } catch (e) {
      if (e.errno != 20) throw e;
    }
  }
};

var FS_unlink = path => withStackSave(() => {
  var buffer = stringToUTF8OnStack(path);
  return __wasmfs_unlink(buffer);
});

var wasmFSDevices = {};

var wasmFSDeviceStreams = {};

var FS = {
  ErrnoError: class extends Error {
    name="ErrnoError";
    message="FS error";
    constructor(code) {
      super();
      this.errno = code;
    }
  },
  handleError(returnValue) {
    // Assume errors correspond to negative returnValues
    // since some functions like _wasmfs_open() return positive
    // numbers on success (some callers of this function may need to negate the parameter).
    if (returnValue < 0) {
      throw new FS.ErrnoError(-returnValue);
    }
    return returnValue;
  },
  createDataFile(parent, name, fileData, canRead, canWrite, canOwn) {
    FS_createDataFile(parent, name, fileData, canRead, canWrite, canOwn);
  },
  createPath(parent, path, canRead, canWrite) {
    // Cache file path directory names.
    var parts = path.split("/").reverse();
    while (parts.length) {
      var part = parts.pop();
      if (!part) continue;
      var current = PATH.join2(parent, part);
      if (!wasmFSPreloadingFlushed) {
        wasmFSPreloadedDirs.push({
          parentPath: parent,
          childName: part
        });
      } else {
        try {
          FS.mkdir(current);
        } catch (e) {
          if (e.errno != 20) throw e;
        }
      }
      parent = current;
    }
    return current;
  },
  createPreloadedFile(parent, name, url, canRead, canWrite, onload, onerror, dontCreateFile, canOwn, preFinish) {
    return FS_createPreloadedFile(parent, name, url, canRead, canWrite, onload, onerror, dontCreateFile, canOwn, preFinish);
  },
  async preloadFile(parent, name, url, canRead, canWrite, dontCreateFile, canOwn, preFinish) {
    return FS_preloadFile(parent, name, url, canRead, canWrite, dontCreateFile, canOwn, preFinish);
  },
  readFile(path, opts = {}) {
    opts.encoding = opts.encoding || "binary";
    if (opts.encoding !== "utf8" && opts.encoding !== "binary") {
      throw new Error(`Invalid encoding type "${opts.encoding}"`);
    }
    var buf, length;
    // Copy the file into a JS buffer on the heap.
    withStackSave(() => {
      var bufPtr = stackAlloc(8);
      var sizePtr = stackAlloc(8);
      FS.handleError(-__wasmfs_read_file(stringToUTF8OnStack(path), bufPtr, sizePtr));
      buf = Number((growMemViews(), HEAPU64)[((bufPtr) >> 3)]);
      length = readI53FromI64(sizePtr);
    });
    // Default return type is binary.
    // The buffer contents exist 8 bytes after the returned pointer.
    return opts.encoding === "utf8" ? UTF8ToString(buf, length) : (growMemViews(), HEAPU8).slice(buf, buf + length);
  },
  cwd: () => UTF8ToString(__wasmfs_get_cwd()),
  analyzePath(path) {
    // TODO: Consider simplifying this API, which for now matches the JS FS.
    var exists = !!FS.findObject(path);
    return {
      exists,
      object: {
        contents: exists ? FS.readFile(path) : null
      }
    };
  },
  mkdir: (path, mode) => FS_mkdir(path, mode),
  mkdirTree: (path, mode) => FS_mkdirTree(path, mode),
  rmdir: path => FS.handleError(withStackSave(() => __wasmfs_rmdir(stringToUTF8OnStack(path)))),
  open: (path, flags, mode = 438) => withStackSave(() => {
    flags = FS_modeStringToFlags(flags);
    var buffer = stringToUTF8OnStack(path);
    var fd = FS.handleError(__wasmfs_open(buffer, flags, mode));
    return {
      fd
    };
  }),
  create: (path, mode) => FS_create(path, mode),
  close: stream => FS.handleError(-__wasmfs_close(stream.fd)),
  unlink: path => FS_unlink(path),
  chdir: path => withStackSave(() => __wasmfs_chdir(stringToUTF8OnStack(path))),
  read(stream, buffer, offset, length, position) {
    var seeking = typeof position != "undefined";
    var dataBuffer = _malloc(length);
    var bytesRead;
    if (seeking) {
      bytesRead = __wasmfs_pread(stream.fd, dataBuffer, length, BigInt(position));
    } else {
      bytesRead = __wasmfs_read(stream.fd, dataBuffer, length);
    }
    if (bytesRead > 0) {
      buffer.set((growMemViews(), HEAPU8).subarray(dataBuffer, dataBuffer + bytesRead), offset);
    }
    _free(dataBuffer);
    return FS.handleError(bytesRead);
  },
  write(stream, buffer, offset, length, position, canOwn) {
    var seeking = typeof position != "undefined";
    var dataBuffer = _malloc(length);
    for (var i = 0; i < length; i++) {
      (growMemViews(), HEAP8)[(dataBuffer) + (i)] = buffer[offset + i];
    }
    var bytesRead;
    if (seeking) {
      bytesRead = __wasmfs_pwrite(stream.fd, dataBuffer, length, BigInt(position));
    } else {
      bytesRead = __wasmfs_write(stream.fd, dataBuffer, length);
    }
    _free(dataBuffer);
    return FS.handleError(bytesRead);
  },
  writeFile: (path, data) => FS_writeFile(path, data),
  mmap: (stream, length, offset, prot, flags) => {
    var buf = FS.handleError(__wasmfs_mmap(length, prot, flags, stream.fd, BigInt(offset)));
    return {
      ptr: buf,
      allocated: true
    };
  },
  msync: (stream, bufferPtr, offset, length, mmapFlags) => {
    assert(!offset);
    // TODO: assert that stream has the fd corresponding to the mapped buffer (bufferPtr).
    return FS.handleError(__wasmfs_msync(bufferPtr, length, mmapFlags));
  },
  munmap: (addr, length) => (FS.handleError(__wasmfs_munmap(addr, length))),
  symlink: (target, linkpath) => withStackSave(() => (__wasmfs_symlink(stringToUTF8OnStack(target), stringToUTF8OnStack(linkpath)))),
  readlink(path) {
    return withStackSave(() => {
      var bufPtr = stackAlloc(8);
      FS.handleError(__wasmfs_readlink(stringToUTF8OnStack(path), bufPtr));
      var readBuffer = Number((growMemViews(), HEAPU64)[((bufPtr) >> 3)]);
      return UTF8ToString(readBuffer);
    });
  },
  statBufToObject(statBuf) {
    // i53/u53 are enough for times and ino in practice.
    return {
      dev: (growMemViews(), HEAPU32)[((statBuf) >> 2)],
      mode: (growMemViews(), HEAPU32)[(((statBuf) + (4)) >> 2)],
      nlink: (growMemViews(), HEAPU64)[(((statBuf) + (8)) >> 3)],
      uid: (growMemViews(), HEAPU32)[(((statBuf) + (16)) >> 2)],
      gid: (growMemViews(), HEAPU32)[(((statBuf) + (20)) >> 2)],
      rdev: (growMemViews(), HEAPU32)[(((statBuf) + (24)) >> 2)],
      size: readI53FromI64((statBuf) + (32)),
      blksize: (growMemViews(), HEAP32)[(((statBuf) + (40)) >> 2)],
      blocks: (growMemViews(), HEAP32)[(((statBuf) + (44)) >> 2)],
      atime: readI53FromI64((statBuf) + (48)),
      mtime: readI53FromI64((statBuf) + (64)),
      ctime: readI53FromI64((statBuf) + (80)),
      ino: readI53FromU64((statBuf) + (96))
    };
  },
  stat(path) {
    return withStackSave(() => {
      var statBuf = stackAlloc(104);
      FS.handleError(__wasmfs_stat(stringToUTF8OnStack(path), statBuf));
      return FS.statBufToObject(statBuf);
    });
  },
  lstat(path) {
    return withStackSave(() => {
      var statBuf = stackAlloc(104);
      FS.handleError(__wasmfs_lstat(stringToUTF8OnStack(path), statBuf));
      return FS.statBufToObject(statBuf);
    });
  },
  chmod(path, mode) {
    return FS.handleError(withStackSave(() => {
      var buffer = stringToUTF8OnStack(path);
      return __wasmfs_chmod(buffer, mode);
    }));
  },
  lchmod(path, mode) {
    return FS.handleError(withStackSave(() => {
      var buffer = stringToUTF8OnStack(path);
      return __wasmfs_lchmod(buffer, mode);
    }));
  },
  fchmod(fd, mode) {
    return FS.handleError(__wasmfs_fchmod(fd, mode));
  },
  utime: (path, atime, mtime) => (FS.handleError(withStackSave(() => (__wasmfs_utime(stringToUTF8OnStack(path), atime, mtime))))),
  truncate(path, len) {
    return FS.handleError(withStackSave(() => (__wasmfs_truncate(stringToUTF8OnStack(path), BigInt(len)))));
  },
  ftruncate(fd, len) {
    return FS.handleError(__wasmfs_ftruncate(fd, BigInt(len)));
  },
  findObject(path) {
    var result = withStackSave(() => __wasmfs_identify(stringToUTF8OnStack(path)));
    if (result == 44) {
      return null;
    }
    return {
      isFolder: result == 31,
      isDevice: false
    };
  },
  readdir: path => withStackSave(() => {
    var pathBuffer = stringToUTF8OnStack(path);
    var entries = [];
    var state = __wasmfs_readdir_start(pathBuffer);
    if (!state) {
      // TODO: The old FS threw an ErrnoError here.
      throw new Error("No such directory");
    }
    var entry;
    while (entry = __wasmfs_readdir_get(state)) {
      entries.push(UTF8ToString(entry));
    }
    __wasmfs_readdir_finish(state);
    return entries;
  }),
  mount: (type, opts, mountpoint) => {
    if (typeof type == "string") {
      // The filesystem was not included, and instead we have an error
      // message stored in the variable.
      throw type;
    }
    var backendPointer = type.createBackend(opts);
    return FS.handleError(withStackSave(() => __wasmfs_mount(stringToUTF8OnStack(mountpoint), backendPointer)));
  },
  unmount: mountpoint => (FS.handleError(withStackSave(() => _wasmfs_unmount(stringToUTF8OnStack(mountpoint))))),
  mknod: (path, mode, dev) => FS_mknod(path, mode, dev),
  makedev: (ma, mi) => ((ma) << 8 | (mi)),
  registerDevice(dev, ops) {
    var backendPointer = _wasmfs_create_jsimpl_backend();
    var definedOps = {
      userRead: ops.read,
      userWrite: ops.write,
      allocFile: file => {
        wasmFSDeviceStreams[file] = {};
      },
      freeFile: file => {
        wasmFSDeviceStreams[file] = undefined;
      },
      getSize: file => {},
      // Devices cannot be resized.
      setSize: (file, size) => 0,
      read: (file, buffer, length, offset) => {
        var bufferArray = (growMemViews(), HEAP8).subarray(buffer, buffer + length);
        try {
          var bytesRead = definedOps.userRead(wasmFSDeviceStreams[file], bufferArray, 0, length, offset);
        } catch (e) {
          return -e.errno;
        }
        (growMemViews(), HEAP8).set(bufferArray, buffer);
        return bytesRead;
      },
      write: (file, buffer, length, offset) => {
        var bufferArray = (growMemViews(), HEAP8).subarray(buffer, buffer + length);
        try {
          var bytesWritten = definedOps.userWrite(wasmFSDeviceStreams[file], bufferArray, 0, length, offset);
        } catch (e) {
          return -e.errno;
        }
        (growMemViews(), HEAP8).set(bufferArray, buffer);
        return bytesWritten;
      }
    };
    wasmFS$backends[backendPointer] = definedOps;
    wasmFSDevices[dev] = backendPointer;
  },
  createDevice(parent, name, input, output) {
    if (typeof parent != "string") {
      // The old API allowed parents to be objects, which do not exist in WasmFS.
      throw new Error("Only string paths are accepted");
    }
    var path = PATH.join2(parent, name);
    var mode = FS_getMode(!!input, !!output);
    FS.createDevice.major ??= 64;
    var dev = FS.makedev(FS.createDevice.major++, 0);
    // Create a fake device with a set of stream ops to emulate
    // the old API's createDevice().
    FS.registerDevice(dev, {
      read(stream, buffer, offset, length, pos) {
        var bytesRead = 0;
        for (var i = 0; i < length; i++) {
          var result;
          try {
            result = input();
          } catch (e) {
            throw new FS.ErrnoError(29);
          }
          if (result === undefined && !bytesRead) {
            throw new FS.ErrnoError(6);
          }
          if (result === null || result === undefined) break;
          bytesRead++;
          buffer[offset + i] = result;
        }
        return bytesRead;
      },
      write(stream, buffer, offset, length, pos) {
        for (var i = 0; i < length; i++) {
          try {
            output(buffer[offset + i]);
          } catch (e) {
            throw new FS.ErrnoError(29);
          }
        }
        return i;
      }
    });
    return FS.mkdev(path, mode, dev);
  },
  mkdev(path, mode, dev) {
    if (typeof dev === "undefined") {
      dev = mode;
      mode = 438;
    }
    var deviceBackend = wasmFSDevices[dev];
    if (!deviceBackend) {
      throw new Error("Invalid device ID.");
    }
    return FS.handleError(withStackSave(() => (_wasmfs_create_file(stringToUTF8OnStack(path), mode, deviceBackend))));
  },
  rename(oldPath, newPath) {
    return FS.handleError(withStackSave(() => {
      var oldPathBuffer = stringToUTF8OnStack(oldPath);
      var newPathBuffer = stringToUTF8OnStack(newPath);
      return __wasmfs_rename(oldPathBuffer, newPathBuffer);
    }));
  },
  llseek(stream, offset, whence) {
    return FS.handleError(__wasmfs_llseek(stream.fd, BigInt(offset), whence));
  }
};

var FS_createPath = FS.createPath;

PThread.init();

if (globalThis.setImmediate) {
  emSetImmediate = setImmediateWrapped;
  emClearImmediate = clearImmediateWrapped;
} else if (globalThis.addEventListener) {
  var __setImmediate_id_counter = 0;
  var __setImmediate_queue = [];
  var __setImmediate_message_id = "_si";
  /** @param {Event} e */ var __setImmediate_cb = e => {
    if (e.data === __setImmediate_message_id) {
      e.stopPropagation();
      __setImmediate_queue.shift()?.();
      ++__setImmediate_id_counter;
    }
  };
  addEventListener("message", __setImmediate_cb, true);
  emSetImmediate = func => {
    if (ENVIRONMENT_IS_WORKER) {
      postMessage(__setImmediate_message_id);
    } else postMessage(__setImmediate_message_id, "*");
    return __setImmediate_id_counter + __setImmediate_queue.push(func) - 1;
  };
  emClearImmediate = /**@type{function(number=)}*/ (id => {
    var index = id - __setImmediate_id_counter;
    if (index < 0 || !__setImmediate_queue[index]) return false;
    // must preserve the order and count of elements in the queue, so replace the pending callback with an empty function
    __setImmediate_queue[index] = null;
    return true;
  });
}

// End JS library code
// include: postlibrary.js
// This file is included after the automatically-generated JS library code
// but before the wasm module is created.
{
  // With WASM_ESM_INTEGRATION this has to happen at the top level and not
  // delayed until processModuleArgs.
  initMemory();
  // Begin ATMODULES hooks
  if (Module["noExitRuntime"]) noExitRuntime = Module["noExitRuntime"];
  if (Module["print"]) out = Module["print"];
  if (Module["printErr"]) err = Module["printErr"];
  // End ATMODULES hooks
  checkIncomingModuleAPI();
  if (Module["arguments"]) programArgs = Module["arguments"];
  if (Module["thisProgram"]) thisProgram = Module["thisProgram"];
  // Assertions on removed incoming Module JS APIs.
  assert(typeof Module["memoryInitializerPrefixURL"] == "undefined", "Module.memoryInitializerPrefixURL option was removed, use Module.locateFile instead");
  assert(typeof Module["pthreadMainPrefixURL"] == "undefined", "Module.pthreadMainPrefixURL option was removed, use Module.locateFile instead");
  assert(typeof Module["cdInitializerPrefixURL"] == "undefined", "Module.cdInitializerPrefixURL option was removed, use Module.locateFile instead");
  assert(typeof Module["filePackagePrefixURL"] == "undefined", "Module.filePackagePrefixURL option was removed, use Module.locateFile instead");
  assert(typeof Module["read"] == "undefined", "Module.read option was removed");
  assert(typeof Module["readAsync"] == "undefined", "Module.readAsync option was removed (modify readAsync in JS)");
  assert(typeof Module["readBinary"] == "undefined", "Module.readBinary option was removed (modify readBinary in JS)");
  assert(typeof Module["setWindowTitle"] == "undefined", "Module.setWindowTitle option was removed (modify emscripten_set_window_title in JS)");
  assert(typeof Module["TOTAL_MEMORY"] == "undefined", "Module.TOTAL_MEMORY has been renamed Module.INITIAL_MEMORY");
  assert(typeof Module["ENVIRONMENT"] == "undefined", "Module.ENVIRONMENT has been deprecated. To force the environment, use the ENVIRONMENT compile-time option (for example, -sENVIRONMENT=web or -sENVIRONMENT=node)");
  assert(typeof Module["STACK_SIZE"] == "undefined", "STACK_SIZE can no longer be set at runtime.  Use -sSTACK_SIZE at link time");
  var preInit = Module["preInit"];
  if (preInit) {
    if (typeof preInit == "function") Module["preInit"] = preInit = [ preInit ];
    // Written as a loop so that preInit functions that themselves add more
    // preInit functions.  Is this actually needed?
    while (preInit.length > 0) {
      preInit.shift()();
    }
  }
  consumedModuleProp("preInit");
}

// Begin runtime exports
Module["callMain"] = callMain;

Module["addRunDependency"] = addRunDependency;

Module["removeRunDependency"] = removeRunDependency;

Module["UTF8ToString"] = UTF8ToString;

Module["stringToUTF8"] = stringToUTF8;

Module["FS_preloadFile"] = FS_preloadFile;

Module["FS_unlink"] = FS_unlink;

Module["FS_createPath"] = FS_createPath;

Module["FS"] = FS;

Module["FS_createDataFile"] = FS_createDataFile;

var missingLibrarySymbols = [ "writeI53ToI64Clamped", "writeI53ToI64Signaling", "writeI53ToU64Clamped", "writeI53ToU64Signaling", "convertI32PairToI53", "convertI32PairToI53Checked", "convertU32PairToI53", "getTempRet0", "createNamedFunction", "zeroMemory", "strError", "inetPton4", "inetNtop4", "inetPton6", "inetNtop6", "readSockaddr", "writeSockaddr", "jstoi_q", "autoResumeAudioContext", "getDynCaller", "dynCall", "asmjsMangle", "mmapAlloc", "addOnInit", "addOnPostCtor", "addOnPreMain", "STACK_SIZE", "STACK_ALIGN", "POINTER_SIZE", "ASSERTIONS", "ccall", "cwrap", "convertJsFunctionToWasm", "getEmptyTableSlot", "updateTableMap", "getFunctionAddress", "addFunction", "removeFunction", "setValue", "getValue", "intArrayToString", "AsciiToString", "stringToAscii", "UTF16ToString", "stringToUTF16", "lengthBytesUTF16", "UTF32ToString", "stringToUTF32", "lengthBytesUTF32", "writeArrayToMemory", "fillDeviceOrientationEventData", "registerDeviceOrientationEventCallback", "fillDeviceMotionEventData", "registerDeviceMotionEventCallback", "screenOrientation", "fillOrientationChangeEventData", "registerOrientationChangeEventCallback", "fillFullscreenChangeEventData", "registerFullscreenChangeEventCallback", "callCanvasResizedCallback", "JSEvents_requestFullscreen", "JSEvents_resizeCanvasForFullscreen", "registerRestoreOldStyle", "hideEverythingExceptGivenElement", "restoreHiddenElements", "setLetterbox", "currentFullscreenStrategy", "softFullscreenResizeWebGLRenderTarget", "doRequestFullscreen", "registerPointerlockErrorEventCallback", "requestPointerLock", "fillGamepadEventData", "registerGamepadEventCallback", "registerBeforeUnloadEventCallback", "fillBatteryEventData", "registerBatteryEventCallback", "setCanvasElementSizeCallingThread", "setCanvasElementSizeMainThread", "setCanvasElementSize", "getCanvasSizeCallingThread", "getCanvasSizeMainThread", "getCanvasElementSize", "convertPCtoSourceLocation", "flush_NO_FILESYSTEM", "wasiRightsToMuslOFlags", "wasiOFlagsToMuslOFlags", "safeClearTimeout", "safeRequestAnimationFrame", "registerPostMainLoop", "registerPreMainLoop", "getPromise", "makePromise", "addPromise", "idsToPromises", "makePromiseCallback", "incrementUncaughtExceptionCount", "decrementUncaughtExceptionCount", "Browser_asyncPrepareDataCounter", "arraySum", "addDays", "wasmfsNodeConvertNodeCode", "wasmfsTry", "wasmfsNodeFixStat", "wasmfsNodeLstat", "wasmfsNodeFstat", "heapObjectForWebGLType", "toTypedArrayIndex", "webgl_enable_ANGLE_instanced_arrays", "webgl_enable_OES_vertex_array_object", "webgl_enable_WEBGL_draw_buffers", "webgl_enable_WEBGL_multi_draw", "webgl_enable_EXT_polygon_offset_clamp", "webgl_enable_EXT_clip_control", "webgl_enable_WEBGL_polygon_mode", "emscriptenWebGLGet", "computeUnpackAlignedImageSize", "colorChannelsInGlTextureFormat", "emscriptenWebGLGetTexPixelData", "emscriptenWebGLGetUniform", "webglGetProgramUniformLocation", "webglGetUniformLocation", "webglPrepareUniformLocationsBeforeFirstUse", "webglGetLeftBracePos", "emscriptenWebGLGetVertexAttrib", "__glGetActiveAttribOrUniform", "writeGLArray", "emscripten_webgl_destroy_context_before_on_calling_thread", "registerWebGlEventCallback", "runAndAbortIfError", "writeStringToMemory", "writeAsciiToMemory", "allocateUTF8", "allocateUTF8OnStack", "demangle", "stackTrace", "getNativeTypeSize", "proxyToMainThreadPtr" ];

missingLibrarySymbols.forEach(missingLibrarySymbol);

var unexportedSymbols = [ "run", "out", "err", "abort", "wasmExports", "writeStackCookie", "checkStackCookie", "writeI53ToI64", "readI53FromI64", "readI53FromU64", "INT53_MAX", "INT53_MIN", "bigintToI53Checked", "HEAP8", "HEAPU8", "HEAP16", "HEAPU16", "HEAP32", "HEAPU32", "HEAPF32", "HEAPF64", "HEAP64", "HEAPU64", "stackSave", "stackRestore", "stackAlloc", "setTempRet0", "ptrToString", "exitJS", "getHeapMax", "growMemory", "ENV", "withStackSave", "ERRNO_CODES", "DNS", "Protocols", "Sockets", "timers", "warnOnce", "readEmAsmArgsArray", "readEmAsmArgs", "runEmAsmFunction", "runMainThreadEmAsm", "getExecutableName", "handleException", "keepRuntimeAlive", "runtimeKeepalivePush", "runtimeKeepalivePop", "callUserCallback", "maybeExit", "asyncLoad", "alignMemory", "HandleAllocator", "wasmTable", "wasmMemory", "getUniqueRunDependency", "noExitRuntime", "addOnPreRun", "addOnExit", "addOnPostRun", "freeTableIndexes", "functionsInTableMap", "PATH", "PATH_FS", "UTF8Decoder", "UTF8ArrayToString", "stringToUTF8Array", "lengthBytesUTF8", "intArrayFromString", "UTF16Decoder", "stringToNewUTF8", "stringToUTF8OnStack", "JSEvents", "registerKeyEventCallback", "specialHTMLTargets", "maybeCStringToJsString", "findEventTarget", "findCanvasEventTarget", "getBoundingClientRect", "fillMouseEventData", "registerMouseEventCallback", "registerWheelEventCallback", "registerUiEventCallback", "registerFocusEventCallback", "restoreOldWindowedStyle", "fillPointerlockChangeEventData", "registerPointerlockChangeEventCallback", "fillVisibilityChangeEventData", "registerVisibilityChangeEventCallback", "registerTouchEventCallback", "jsStackTrace", "getCallstack", "UNWIND_CACHE", "ExitStatus", "getEnvStrings", "checkWasiClock", "initRandomFill", "randomFill", "safeSetTimeout", "setImmediateWrapped", "clearImmediateWrapped", "emSetImmediate", "emClearImmediate_deps", "emClearImmediate", "promiseMap", "uncaughtExceptionCount", "exceptionLast", "exceptionCaught", "ExceptionInfo", "findMatchingCatch", "getExceptionMessageCommon", "incrementExceptionRefcount", "decrementExceptionRefcount", "getExceptionMessage", "Browser", "requestFullscreen", "setCanvasSize", "getUserMedia", "createContext", "getPreloadedImageData__data", "wget", "MONTH_DAYS_REGULAR", "MONTH_DAYS_LEAP", "MONTH_DAYS_REGULAR_CUMULATIVE", "MONTH_DAYS_LEAP_CUMULATIVE", "isLeapYear", "ydayFromDate", "preloadPlugins", "FS_createPreloadedFile", "FS_modeStringToFlags", "FS_getMode", "FS_fileDataToTypedArray", "FS_stdin_getChar_buffer", "FS_stdin_getChar", "FS_createDevice", "FS_readFile", "MEMFS", "wasmFSPreloadedFiles", "wasmFSPreloadedDirs", "wasmFSPreloadingFlushed", "wasmFSDevices", "wasmFSDeviceStreams", "FS_mknod", "FS_create", "FS_writeFile", "FS_mkdir", "FS_mkdirTree", "wasmFS$JSMemoryFiles", "wasmFS$backends", "wasmFS$JSMemoryRanges", "wasmfsNodeIsWindows", "wasmfsOPFSDirectoryHandles", "wasmfsOPFSFileHandles", "wasmfsOPFSAccessHandles", "wasmfsOPFSBlobs", "wasmfsOPFSProxyFinish", "wasmfsOPFSGetOrCreateFile", "wasmfsOPFSGetOrCreateDir", "tempFixedLengthArray", "miniTempWebGLFloatBuffers", "miniTempWebGLIntBuffers", "GL", "AL", "GLUT", "EGL", "GLEW", "IDBStore", "SDL", "SDL_gfx", "waitAsyncPolyfilled", "print", "printErr", "jstoi_s", "PThread", "terminateWorker", "cleanupThread", "registerTLSInit", "spawnThread", "exitOnMainThread", "proxyToMainThread", "proxiedJSCallArgs", "invokeEntryPoint", "checkMailbox", "WebGPU", "emwgpuStringToInt_BufferMapState", "emwgpuStringToInt_CompilationMessageType", "emwgpuStringToInt_DeviceLostReason", "emwgpuStringToInt_FeatureName", "emwgpuStringToInt_PreferredFormat" ];

unexportedSymbols.forEach(unexportedRuntimeSymbol);

// End runtime exports
// Begin JS library exports
// End JS library exports
// end include: postlibrary.js
// proxiedFunctionTable specifies the list of functions that can be called
// either synchronously or asynchronously from other threads in postMessage()d
// or internally queued events. This way a pthread in a Worker can synchronously
// access e.g. the DOM on the main thread.
var proxiedFunctionTable = [ _proc_exit, exitOnMainThread, pthreadCreateProxied, _emscripten_get_visibility_status, _emscripten_set_blur_callback_on_thread, _emscripten_set_keydown_callback_on_thread, _emscripten_set_keyup_callback_on_thread, _emscripten_set_mousedown_callback_on_thread, _emscripten_set_mouseenter_callback_on_thread, _emscripten_set_mousemove_callback_on_thread, _emscripten_set_mouseup_callback_on_thread, _emscripten_set_pointerlockchange_callback_on_thread, _emscripten_set_resize_callback_on_thread, _emscripten_set_touchcancel_callback_on_thread, _emscripten_set_touchend_callback_on_thread, _emscripten_set_touchmove_callback_on_thread, _emscripten_set_touchstart_callback_on_thread, _emscripten_set_visibilitychange_callback_on_thread, _emscripten_set_wheel_callback_on_thread, _environ_get, _environ_sizes_get ];

function checkIncomingModuleAPI() {
  ignoredModuleProp("fetchSettings");
  ignoredModuleProp("logReadFiles");
  ignoredModuleProp("loadSplitModule");
  ignoredModuleProp("onMalloc");
  ignoredModuleProp("onRealloc");
  ignoredModuleProp("onFree");
  ignoredModuleProp("onSbrkGrow");
  ignoredModuleProp("onCOSCacheHit");
  ignoredModuleProp("onCOSCacheMiss");
  ignoredModuleProp("onCOSStore");
  ignoredModuleProp("GL_MAX_TEXTURE_IMAGE_UNITS");
  ignoredModuleProp("SDL_canPlayWithWebAudio");
  ignoredModuleProp("SDL_numSimultaneouslyQueuedBuffers");
  ignoredModuleProp("freePreloadedMediaOnUse");
  ignoredModuleProp("preinitializedWebGLContext");
  ignoredModuleProp("keyboardListeningElement");
  ignoredModuleProp("doNotCaptureKeyboard");
  ignoredModuleProp("extraStackTrace");
  ignoredModuleProp("preloadPlugins");
  ignoredModuleProp("preMainLoop");
  ignoredModuleProp("postMainLoop");
  ignoredModuleProp("forcedAspectRatio");
  ignoredModuleProp("onFullScreen");
  ignoredModuleProp("INITIAL_MEMORY");
  ignoredModuleProp("wasmBinary");
}

var ASM_CONSTS = {
  39084405: $0 => {
    var url = UTF8ToString($0);
    var w = null;
    try {
      w = window.open(url, "_blank");
    } catch (e) {
      w = null;
    }
    if (w) {
      try {
        w.opener = null;
      } catch (e) {}
      return 1;
    }
    try {
      window.dispatchEvent(new CustomEvent("ue-open-url", {
        detail: {
          url,
          blocked: true
        }
      }));
    } catch (e) {}
    return 2;
  },
  39084700: () => {
    var Fixed = Module["ueFixedMemory"];
    return (Fixed && Fixed["memory"] === wasmMemory && Number.isInteger(Fixed["pages"]) && Fixed["pages"] >= 1) ? Fixed["pages"] : 0;
  },
  39084871: () => performance.now(),
  39084901: $0 => {
    globalThis["ueLogFile"] = UTF8ToString(Number($0));
  },
  39084957: () => {
    if (typeof window !== "undefined") window.dispatchEvent(new Event("ue-first-canvas-frame"));
  },
  39085054: () => {
    var Agent = (typeof navigator !== "undefined" && navigator.userAgent) ? navigator.userAgent : "";
    return (Agent.indexOf("AppleWebKit") >= 0 && Agent.indexOf("Chrome/") < 0 && Agent.indexOf("Chromium/") < 0) ? 1 : 0;
  },
  39085274: () => navigator.userAgent.indexOf("Firefox") < 0 ? 1 : 0,
  39085337: () => {
    var U = navigator.userAgent;
    var Apple = typeof navigator.vendor === "string" && navigator.vendor.indexOf("Apple") === 0;
    if (!Apple && (U.indexOf("AppleWebKit") < 0 || U.indexOf("Chrome") >= 0)) return -1;
    var Major = 0;
    var Os = /(?:iPhone OS|CPU OS) ([0-9]+)_([0-9]+)/.exec(U);
    if (Os && Os[1] != "18") Major = parseInt(Os[1], 10);
    var At = U.indexOf(" Version/");
    var Safari = At >= 0 ? parseInt(U.substring(At + 9), 10) : 0;
    if (Safari > Major) Major = Safari;
    return Major;
  },
  39085821: () => (typeof navigator !== "undefined" && navigator.deviceMemory) ? navigator.deviceMemory : 0,
  39085923: $0 => {
    if (typeof window !== "undefined" && window.dispatchEvent) window.dispatchEvent(new CustomEvent(UTF8ToString($0)));
  },
  39086043: ($0, $1) => {
    if (typeof window !== "undefined" && window.dispatchEvent) window.dispatchEvent(new CustomEvent("ue-exit", {
      detail: {
        code: $0,
        done: !!$1
      }
    }));
  },
  39086194: $0 => {
    if (typeof window !== "undefined" && window.dispatchEvent) window.dispatchEvent(new CustomEvent("ue-hold", {
      detail: {
        capMs: $0
      }
    }));
  },
  39086334: ($0, $1) => {
    if (typeof window !== "undefined" && window.dispatchEvent) window.dispatchEvent(new CustomEvent("ue-settled", {
      detail: {
        heldMs: $0,
        reason: UTF8ToString($1)
      }
    }));
  },
  39086504: ($0, $1) => {
    if (typeof window !== "undefined" && window.dispatchEvent) window.dispatchEvent(new CustomEvent(UTF8ToString($0), {
      detail: {
        fraction: $1
      }
    }));
  },
  39086654: $0 => {
    if (typeof document !== "undefined") {
      var Title = document.title;
      var Open = Title.lastIndexOf(" (");
      document.title = (Open >= 0 && Title.endsWith(")") ? Title.substring(0, Open) : Title) + " (" + UTF8ToString($0) + ")";
    }
  },
  39086883: ($0, $1, $2) => {
    (function() {
      const id = $0, url = UTF8ToString(Number($1)), body = UTF8ToString(Number($2));
      const done = (ok, text) => {
        const p = stringToNewUTF8(text);
        _PhoenixSessionResponse(id, ok, BigInt(p));
        _free(p);
      };
      fetch(url, {
        method: "POST",
        headers: {
          "Content-Type": "application/json"
        },
        body,
        signal: AbortSignal.timeout(2e4)
      }).then(async r => done(r.ok ? 1 : 0, await r.text())).catch(e => done(0, String(e)));
    })();
  },
  39087264: ($0, $1, $2, $3) => {
    (function() {
      const id = $0, url = UTF8ToString(Number($1)), hello = JSON.parse(UTF8ToString(Number($2))), force = !!$3;
      const all = Module.phoenixLinks || (Module.phoenixLinks = {});
      const s = {
        ws: new WebSocket(url),
        peers: new Map,
        pending: [],
        closed: false,
        ready: false
      };
      all[id] = s;
      const emit = m => {
        const p = stringToNewUTF8(JSON.stringify(m));
        _PhoenixTransportReceive(id, BigInt(p));
        _free(p);
      };
      const send = m => {
        if (s.ready && s.ws.readyState === 1 && s.ws.bufferedAmount < 4194304) s.ws.send(JSON.stringify(m));
      };
      const peer = (remote, port, initiator) => {
        const key = remote + "/" + port;
        if (s.peers.has(key)) {
          const old = s.peers.get(key);
          if (old.pc.connectionState === "failed" || old.dc?.readyState === "closed") {
            old.pc.close();
            s.peers.delete(key);
          } else return old;
        }
        const p = {
          pc: new RTCPeerConnection({
            iceServers: [ {
              urls: "stun:stun.l.google.com:19302"
            } ]
          }),
          dc: null,
          chain: Promise.resolve(),
          candidates: [],
          created: performance.now()
        };
        s.peers.set(key, p);
        send({
          type: "connect",
          to: remote,
          port,
          payload: ""
        });
        const attach = dc => {
          p.dc = dc;
          dc.onmessage = e => {
            try {
              emit({
                type: "packet",
                from: remote,
                port,
                payload: e.data,
                route: "webrtc"
              });
            } catch {}
          };
        };
        p.pc.ondatachannel = e => attach(e.channel);
        p.pc.onicecandidate = e => {
          if (e.candidate) send({
            type: "candidate",
            to: remote,
            port,
            payload: e.candidate.candidate,
            mid: e.candidate.sdpMid || "0"
          });
        };
        if (initiator) {
          attach(p.pc.createDataChannel("phoenix", hello.port === 9001 ? {
            ordered: true
          } : {
            ordered: false,
            maxRetransmits: 0
          }));
          p.chain = p.pc.createOffer().then(d => p.pc.setLocalDescription(d)).then(() => send({
            type: "offer",
            to: remote,
            port,
            payload: p.pc.localDescription.sdp
          })).catch(() => {});
        }
        return p;
      };
      s.send = m => {
        if (!s.ready) return;
        if (m.type === "packet" && !force) {
          const p = peer(m.to, m.port, hello.peer < m.to);
          if (p.dc && p.dc.readyState === "open") {
            if (p.dc.bufferedAmount < 4194304) p.dc.send(m.payload);
            return;
          }
          if (performance.now() - (p.lastConnectSent || 0) > 1e3) {
            p.lastConnectSent = performance.now();
            send({
              type: "connect",
              to: m.to,
              port: m.port,
              payload: ""
            });
          }
          if (performance.now() - p.created < 2500) return;
        }
        send(m);
      };
      const bind = () => {
        s.ws.onopen = () => s.ws.send(JSON.stringify(hello));
        s.ws.onmessage = e => {
          let m;
          try {
            m = JSON.parse(e.data);
          } catch {
            return;
          }
          if (m.type === "ready") {
            s.ready = true;
            emit(m);
            return;
          }
          if (m.type === "packet") {
            m.route = "relay";
            emit(m);
            return;
          }
          if (force) return;
          const p = peer(m.from, m.port, m.type === "connect" && hello.peer < m.from);
          if (m.type === "connect") {
            if (p.pc.localDescription && p.dc?.readyState !== "open" && performance.now() - (p.lastDescriptionSent || 0) > 1e3) {
              p.lastDescriptionSent = performance.now();
              send({
                type: p.pc.localDescription.type,
                to: m.from,
                port: m.port,
                payload: p.pc.localDescription.sdp
              });
            }
            return;
          }
          p.chain = p.chain.then(async () => {
            if (m.type === "candidate") {
              if (p.pc.remoteDescription) await p.pc.addIceCandidate({
                candidate: m.payload,
                sdpMid: m.mid
              }); else p.candidates.push(m);
              return;
            }
            await p.pc.setRemoteDescription({
              type: m.type,
              sdp: m.payload
            });
            for (const c of p.candidates) await p.pc.addIceCandidate({
              candidate: c.payload,
              sdpMid: c.mid
            });
            p.candidates = [];
            if (m.type === "offer") {
              await p.pc.setLocalDescription(await p.pc.createAnswer());
              send({
                type: "answer",
                to: m.from,
                port: m.port,
                payload: p.pc.localDescription.sdp
              });
            }
          }).catch(() => {});
        };
        s.ws.onerror = () => emit({
          type: "error",
          payload: "relay-error"
        });
        s.ws.onclose = () => {
          s.ready = false;
          emit({
            type: "error",
            payload: "relay-closed"
          });
          if (!s.closed) s.retry = setTimeout(() => {
            if (!s.closed) {
              s.ws = new WebSocket(url);
              bind();
            }
          }, 2e3);
        };
      };
      bind();
    })();
  },
  39090570: ($0, $1) => {
    (function() {
      const s = Module.phoenixLinks && Module.phoenixLinks[$0];
      if (s && !s.closed) s.send(JSON.parse(UTF8ToString(Number($1))));
    })();
  },
  39090706: $0 => {
    (function() {
      const s = Module.phoenixLinks && Module.phoenixLinks[$0];
      if (s) {
        s.closed = true;
        s.ws.onmessage = null;
        s.ws.onclose = null;
        s.ws.onerror = null;
        s.ws.close();
        for (const p of s.peers.values()) p.pc.close();
        delete Module.phoenixLinks[$0];
      }
    })();
  },
  39090947: $0 => {
    (function() {
      const s = Module.phoenixStores && Module.phoenixStores[$0];
      if (s) {
        if (s.db) s.db.close();
        s.release();
        delete Module.phoenixStores[$0];
      }
    })();
  },
  39091096: ($0, $1, $2) => {
    (function() {
      const id = $0, g = $1, text = UTF8ToString(Number($2)), s = Module.phoenixStores && Module.phoenixStores[id];
      const finish = ok => _PhoenixFleetStorage(id, g, ok, 0n);
      if (!s || !s.db) {
        finish(0);
        return;
      }
      try {
        const tx = s.db.transaction("hosts", "readwrite", {
          durability: "strict"
        });
        if (tx.durability !== "strict") {
          tx.abort();
          finish(0);
          return;
        }
        tx.objectStore("hosts").put(text, s.key);
        tx.oncomplete = () => finish(1);
        tx.onabort = () => finish(0);
        tx.onerror = () => {};
      } catch {
        finish(0);
      }
    })();
  },
  39091556: ($0, $1) => {
    (function() {
      const id = $0, key = UTF8ToString(Number($1)), all = Module.phoenixStores || (Module.phoenixStores = {});
      const emit = (text, ok) => {
        const p = stringToNewUTF8(text);
        _PhoenixFleetStorage(id, 0, ok, BigInt(p));
        _free(p);
      };
      if (!navigator.locks) {
        emit("Exclusive browser storage locks unavailable", 0);
        return;
      }
      navigator.locks.request("phoenix-" + key, {
        ifAvailable: true
      }, async lock => {
        if (!lock) {
          emit("Session already open in another tab", 0);
          return;
        }
        await new Promise(release => {
          const state = {
            release,
            key,
            db: null
          };
          all[id] = state;
          const req = indexedDB.open("phoenix-v4", 1);
          req.onupgradeneeded = () => req.result.createObjectStore("hosts");
          req.onerror = () => emit("IndexedDB open failed", 0);
          req.onsuccess = () => {
            state.db = req.result;
            const tx = state.db.transaction("hosts", "readonly");
            const get = tx.objectStore("hosts").get(key);
            get.onsuccess = () => emit(get.result || "null", 1);
            get.onerror = () => emit("IndexedDB read failed", 0);
          };
        });
      });
    })();
  },
  39092460: ($0, $1, $2, $3) => {
    (function() {
      const id = $0, url = UTF8ToString(Number($1)), hash = UTF8ToString(Number($2)), limit = $3;
      const finish = (ok, bytes) => {
        const p = bytes.length ? _malloc(bytes.length) : 0;
        if (p) (growMemViews(), HEAPU8).set(bytes, Number(p));
        _PhoenixModDownloaded(id, ok, BigInt(p), bytes.length);
        if (p) _free(p);
      };
      (async () => {
        let cache = null, response = null, hit = false;
        if (hash) {
          cache = await caches.open("phoenix-v4-mods-v1");
          response = await cache.match(url + "?phoenixHash=" + hash);
          hit = !!response;
        }
        if (!response) response = await fetch(url, {
          signal: AbortSignal.timeout(6e4)
        });
        if (!response.ok) throw Error("Download refused");
        const reader = response.body.getReader(), chunks = [];
        let size = 0;
        while (true) {
          const {done, value} = await reader.read();
          if (done) break;
          size += value.length;
          if (size > limit) {
            await reader.cancel();
            throw Error("Mod exceeds limit");
          }
          chunks.push(value);
        }
        const bytes = new Uint8Array(size);
        let offset = 0;
        for (const chunk of chunks) {
          bytes.set(chunk, offset);
          offset += chunk.length;
        }
        if (hash) {
          const digest = Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", bytes)), b => b.toString(16).padStart(2, "0")).join("");
          if (digest !== hash.toLowerCase()) throw Error("Hash mismatch");
          if (!hit) await cache.put(url + "?phoenixHash=" + hash, new Response(bytes)); else console.log("PHOENIX_MOD_BROWSER_CACHE_HIT", hash);
        }
        finish(1, bytes);
      })().catch(() => finish(0, new Uint8Array));
    })();
  },
  39093778: $0 => {
    (function() {
      const text = UTF8ToString(Number($0));
      const url = URL.createObjectURL(new Blob([ text ], {
        type: "application/json"
      }));
      const a = document.createElement("a");
      a.href = url;
      a.download = "phoenix-session.json";
      document.body.append(a);
      a.click();
      a.remove();
      setTimeout(() => URL.revokeObjectURL(url), 1e3);
    })();
  },
  39094082: () => {
    (function() {
      const input = document.createElement("input");
      input.type = "file";
      input.accept = ".json,application/json";
      input.setAttribute("aria-label", "Restore Phoenix snapshot");
      input.style.display = "none";
      document.body.append(input);
      input.onchange = async () => {
        try {
          const file = input.files[0];
          if (!file) return;
          if (file.size > 8 * 1024 * 1024) {
            alert("Snapshot exceeds the 8 MiB limit.");
            return;
          }
          const text = await file.text();
          const p = stringToNewUTF8(text);
          _PhoenixImportSnapshot(BigInt(p));
          _free(p);
        } finally {
          input.remove();
        }
      };
      input.oncancel = () => input.remove();
      input.click();
    })();
  },
  39094644: $0 => {
    (function() {
      const definitions = JSON.parse(UTF8ToString(Number($0)));
      window.removeEventListener("blur", Module.phoenixStationBlur);
      document.getElementById("phoenix-station")?.remove();
      document.getElementById("phoenix-station-style")?.remove();
      const style = document.createElement("style");
      style.id = "phoenix-station-style";
      style.textContent = ` body #phoenix-station{position:fixed;right:0;top:0;bottom:0;width:min(480px,100%);overflow:auto;z-index:10000;box-sizing:border-box;background:#091927;color:#f0f7ff;font:18px system-ui;padding:20px;box-shadow:0 0 20px #0008} body #phoenix-station label{display:block;margin:12px 0}#phoenix-station select,#phoenix-station button,#phoenix-station input{font:inherit;min-height:44px;max-width:100%;box-sizing:border-box} body #phoenix-station button{margin:4px;padding:8px 14px;background:#dceeff;color:#072136;border:2px solid #aacfff;border-radius:5px} body #phoenix-station input[type=range]{width:100%}#phoenix-station :focus-visible{outline:3px solid #ffe073;outline-offset:3px}#phoenix-station button:disabled{opacity:.5}#phoenix-station output{display:block;white-space:pre-wrap;margin:14px 0;line-height:1.5} @media(max-width:600px){#phoenix-station{width:100%;padding:16px}}`;
      document.head.append(style);
      const root = document.createElement("section");
      root.id = "phoenix-station";
      root.setAttribute("aria-label", "Phoenix station controls");
      root.innerHTML = '<h1>Phoenix station</h1><label>Ship <select id="px-ship" aria-label="Ship"><option value="1">Ship 1</option><option value="2">Ship 2</option></select></label><label>Station <select id="px-seat" aria-label="Station"><option value="0">Helm</option><option value="1">Tactical</option><option value="2">Engineering</option><option value="3">Science</option><option value="4">Comms</option></select></label><output id="px-state" aria-label="Station observations"></output><div><button id="px-claim">Take control</button><button id="px-release">Release to AI</button></div><fieldset id="px-actions"><legend>Station commands</legend><div id="px-controls"></div></fieldset><p id="px-receipt" role="status" aria-live="polite"></p>';
      document.body.append(root);
      const find = id => document.getElementById("px-" + id);
      const send = (action, value = 0, target = 0) => _PhoenixWebAction(action, Number(value), Number(target));
      const stationSelect = find("seat");
      stationSelect.replaceChildren();
      definitions.forEach((d, i) => {
        const option = document.createElement("option");
        option.value = i;
        option.textContent = d.label;
        stationSelect.append(option);
      });
      const authored = (controller, action, value = 0, target = 0) => _PhoenixWebAuthoredAction(Number(find("seat").value), controller, action, Number(value), Number(target));
      const render = (definition, c, dispatch, lifecycle = false) => {
        c.replaceChildren();
        definition.controllers.forEach((controller, ci) => {
          const group = document.createElement("fieldset");
          group.dataset.controller = controller.id;
          const legend = document.createElement("legend");
          legend.textContent = controller.id;
          group.append(legend);
          c.append(group);
          if (ci || lifecycle) {
            for (const [text, id] of [ [ lifecycle ? "Take GM control" : "Take control", -1 ], [ "Release to AI", -2 ] ]) {
              const button = document.createElement("button");
              button.textContent = text;
              button.dataset.lifecycle = id;
              button.onclick = () => dispatch(ci, id);
              group.append(button);
            }
          }
          controller.actions.forEach((a, ai) => {
            const row = document.createElement("div");
            row.dataset.action = a.id;
            row.dataset.controller = controller.id;
            let value = a.value, target = a.target;
            const input = (text, type, min, max, initial, change) => {
              const label = document.createElement("label");
              label.textContent = text;
              const element = document.createElement("input");
              element.type = type;
              element.setAttribute("aria-label", text);
              element.min = min;
              element.max = max;
              element.step = 1;
              element.value = initial;
              element.oninput = () => change(Number(element.value));
              label.append(element);
              row.append(label);
              return element;
            };
            if (a.slider) {
              const element = input(a.label, "range", a.min, a.max, a.value, v => dispatch(ci, ai, v, target));
              element.dataset.field = a.field;
            } else {
              if (!a.fixed && (a.payload === 1 || a.payload === 3)) input(a.label + " value", "number", a.min, a.max, a.value, v => value = v);
              if (!a.fixed && (a.payload === 2 || a.payload === 3)) input(a.label + " target", "number", 1, 2147483646, a.target, v => target = v);
              const button = document.createElement("button");
              button.textContent = a.label;
              button.onclick = () => dispatch(ci, ai, value, target);
              row.append(button);
            }
            group.append(row);
          });
        });
      };
      const controls = () => render(definitions[Number(find("seat").value)], find("controls"), authored);
      Module.phoenixRenderControls = render;
      find("ship").onchange = () => send(12, find("ship").value);
      find("seat").onchange = () => {
        send(11, find("seat").value);
        controls();
      };
      find("claim").onclick = () => authored(0, -1);
      find("release").onclick = () => authored(0, -2);
      controls();
      Module.phoenixStationUpdate = (frame, receipt, operator) => {
        if (find("receipt").textContent !== receipt) find("receipt").textContent = receipt;
        let v;
        try {
          v = JSON.parse(frame);
        } catch {
          return;
        }
        root.phoenixObservation = v;
        if (!v.station) {
          find("actions").disabled = true;
          find("claim").disabled = true;
          find("release").disabled = true;
          return;
        }
        const lines = [ v.station + " — " + (v.owner ? "Crew controlled" : "AI watch"), v.fleetReady === false ? "Waiting for hosts to load the scenario" : v.paused ? "Mission paused" : "Mission active" ];
        if ("heading" in v && "thrust" in v) lines.push("Heading " + v.heading + "°, thrust " + v.thrust + "%, position " + v.x + ", " + v.y);
        if ("hull" in v && "power" in v) lines.push("Hull " + v.hull + "%, power " + v.power + "%, repair " + (v.repair / 20).toFixed(1) + " seconds remaining");
        if (definitions[Number(find("seat").value)].fields.includes("finding")) lines.push(v.finding || "Contact 3 has not been scanned.");
        if ("target" in v) {
          lines.push((v.weaponLabel || "Projectile weapon") + ", target " + (v.target || "none"));
          const fire = root.querySelector('[data-action="fire"] button');
          if (fire) fire.textContent = "Fire " + (v.weaponLabel || "projectile");
        }
        if ("objectiveAcknowledged" in v) lines.push(v.message || "No incoming messages.", v.objectiveAcknowledged ? "Objective acknowledged" : "Objective awaiting acknowledgement");
        find("state").textContent = lines.join("\n");
        const waiting = v.fleetReady === false;
        find("actions").disabled = waiting;
        find("controls").querySelectorAll("[data-controller]").forEach(e => {
          const lease = v.controllers?.find(c => c.id === e.dataset.controller);
          if (e.tagName === "FIELDSET") {
            e.querySelectorAll("[data-action]").forEach(a => a.querySelectorAll("button,input").forEach(w => w.disabled = waiting || !operator || lease?.owner !== operator));
            e.querySelectorAll("[data-lifecycle]").forEach(b => b.disabled = waiting || (b.dataset.lifecycle === "-1" ? !!lease?.owner && lease.owner !== operator : lease?.owner !== operator));
          }
        });
        root.querySelectorAll("input[data-field]").forEach(input => {
          if (document.activeElement !== input && typeof v[input.dataset.field] === "number") input.value = v[input.dataset.field];
        });
        find("release").disabled = waiting || v.owner !== operator;
        find("claim").disabled = waiting || !!v.owner && v.owner !== operator;
        if (find("receipt").textContent !== receipt) find("receipt").textContent = receipt;
      };
      Module.phoenixStationBlur = () => {
        if (Number(find("seat")?.value) === 0) send(3);
      };
      window.addEventListener("blur", Module.phoenixStationBlur);
      root.addEventListener("pointercancel", Module.phoenixStationBlur);
    })();
  },
  39101778: ($0, $1) => {
    (function() {
      const root = document.getElementById("phoenix-station");
      if (!root) return;
      const code = document.createElement("p");
      code.id = "px-join-code";
      code.textContent = "Join code: " + UTF8ToString(Number($0));
      root.append(code);
      const box = document.createElement("details"), summary = document.createElement("summary");
      box.id = "px-host";
      summary.textContent = "Host and GM controls";
      box.append(summary);
      const info = document.createElement("output");
      info.id = "px-gm-observations";
      info.setAttribute("aria-label", "GM observations");
      box.append(info);
      const controls = document.createElement("div");
      controls.id = "px-gm-controls";
      box.append(controls);
      Module.phoenixRenderControls(JSON.parse(UTF8ToString(Number($1))), controls, (c, a, v = 0, t = 0) => _PhoenixWebAuthoredAction(5, c, a, Number(v), Number(t)), true);
      [ "Export save", "Recover from save" ].forEach((text, i) => {
        const button = document.createElement("button");
        button.textContent = text;
        button.onclick = () => _PhoenixWebHostAction(i + 6);
        box.append(button);
      });
      root.append(box);
    })();
  },
  39102766: ($0, $1) => {
    (function() {
      try {
        const view = JSON.parse(UTF8ToString(Number($0))), output = document.getElementById("px-gm-observations");
        if (output) output.textContent = (view.finding || "") + " | " + (view.entities || 0) + " entities | tick " + (view.tick || 0);
        const operator = UTF8ToString(Number($1));
        document.querySelectorAll("#px-gm-controls fieldset").forEach(e => {
          const lease = view.controllers?.find(c => c.id === e.dataset.controller);
          e.querySelectorAll("[data-action] button,[data-action] input").forEach(w => w.disabled = view.fleetReady === false || lease?.owner !== operator);
          e.querySelectorAll("[data-lifecycle]").forEach(b => b.disabled = view.fleetReady === false || (b.dataset.lifecycle === "-1" ? !!lease?.owner && lease.owner !== operator : lease?.owner !== operator));
        });
      } catch {}
    })();
  },
  39103499: ($0, $1, $2) => {
    Module.phoenixStationUpdate?.(UTF8ToString(Number($0)), UTF8ToString(Number($1)), UTF8ToString(Number($2)));
  },
  39103608: () => {
    document.getElementById("phoenix-station")?.remove();
    window.removeEventListener("blur", Module.phoenixStationBlur);
    delete Module.phoenixStationUpdate;
  },
  39103760: () => {
    try {
      const Ua = navigator.userAgent || "";
      const Touch = navigator.maxTouchPoints || 0;
      if (/iPhone|iPad|iPod/.test(Ua) || (/Macintosh/.test(Ua) && Touch > 1)) {
        return 1;
      }
      if (/Android/.test(Ua)) {
        return 2;
      }
      if (/Mobi|Tablet/.test(Ua)) {
        return 3;
      }
      if (/Windows/.test(Ua)) {
        return 4;
      }
      if (/Macintosh|Mac OS X/.test(Ua)) {
        return 5;
      }
      if (/Linux|CrOS|X11/.test(Ua)) {
        return 6;
      }
    } catch (Error) {}
    return 0;
  }
};

function WasmTrace_Open(Id, State, HostText, Port, PageClosing) {
  var Sockets = Module["ueTrace"] = Module["ueTrace"] || {};
  if (!Module["ueTraceHide"] && typeof addEventListener === "function") {
    Module["ueTraceHide"] = true;
    addEventListener("pagehide", function() {
      Atomics.store(new Int32Array((growMemViews(), HEAP8).buffer, PageClosing, 1), 0, 1);
    });
  }
  var Relay = "";
  var Args = Module["arguments"] || [];
  for (var I = 0; I < Args.length; ++I) {
    var Arg = String(Args[I]);
    if (Arg.toLowerCase().indexOf("-webrelay=") === 0) {
      Relay = Arg.substring(10);
    }
  }
  var Scheme = location.protocol === "https:" ? "wss://" : "ws://";
  if (!Relay) {
    Relay = Scheme + location.hostname + ":8889";
  } else if (Relay.indexOf("://") < 0) {
    Relay = Scheme + Relay;
  }
  var Host = UTF8ToString(HostText);
  if (Host === "localhost") {
    Host = "127.0.0.1";
  }
  var S = {
    State,
    Pending: [],
    Closing: false,
    Ws: null,
    Timer: 0,
    Sent: 0
  };
  Sockets[Id] = S;
  var Url = Relay + "/tcp?to=" + Host + ":" + Port;
  try {
    S.Ws = new WebSocket(Url);
  } catch (Error) {
    console.log("WEB TRACE could not open " + Url + ": " + Error);
    Atomics.store(new Int32Array((growMemViews(), HEAP8).buffer, State, 2), 0, 2);
    S.State = 0;
    delete Sockets[Id];
    return;
  }
  S.Ws.binaryType = "arraybuffer";
  S.Ws.onopen = function() {
    console.log("WEB TRACE connected to " + Host + ":" + Port + " through " + Relay);
    if (S.State) {
      Atomics.compareExchange(new Int32Array((growMemViews(), HEAP8).buffer, S.State, 2), 0, 0, 1);
    }
    Module["ueTracePump"](Id);
  };
  S.Ws.onerror = function() {
    console.log("WEB TRACE relay error for " + Url);
  };
  S.Ws.onclose = function(Event) {
    console.log("WEB TRACE closed (code " + Event.code + ", " + S.Sent + " bytes sent)");
    if (S.State) {
      Atomics.store(new Int32Array((growMemViews(), HEAP8).buffer, S.State, 2), 0, 2);
    }
    if (S.Timer) {
      clearTimeout(S.Timer);
      S.Timer = 0;
    }
    S.Pending = [];
    delete Sockets[Id];
  };
  if (!Module["ueTracePump"]) {
    Module["ueTracePump"] = function(PumpId) {
      var P = Module["ueTrace"][PumpId];
      if (!P) {
        return;
      }
      P.Timer = 0;
      if (!P.Ws || P.Ws.readyState !== 1) {
        return;
      }
      while (P.Pending.length && P.Ws.bufferedAmount < (4 << 20)) {
        var Chunk = P.Pending.shift();
        try {
          P.Ws.send(Chunk);
        } catch (Error) {
          console.log("WEB TRACE send failed: " + Error);
          if (P.State) {
            Atomics.store(new Int32Array((growMemViews(), HEAP8).buffer, P.State, 2), 0, 2);
          }
          P.Pending = [];
          P.Ws.close();
          return;
        }
        P.Sent += Chunk.length;
        if (P.State) {
          Atomics.sub(new Int32Array((growMemViews(), HEAP8).buffer, P.State, 2), 1, Chunk.length);
        }
      }
      if (P.Pending.length) {
        P.Timer = setTimeout(function() {
          Module["ueTracePump"](PumpId);
        }, 2);
      } else if (P.Closing) {
        P.Ws.close();
      }
    };
  }
}

function WasmTrace_Send(Id, Data, Size) {
  var S = Module["ueTrace"] && Module["ueTrace"][Id];
  if (!S || S.Closing) {
    return;
  }
  for (var Offset = 0; Offset < Size; Offset += 6e4) {
    var End = Math.min(Size, Offset + 6e4);
    S.Pending.push((growMemViews(), HEAPU8).slice(Data + Offset, Data + End));
  }
  if (!S.Timer) {
    Module["ueTracePump"](Id);
  }
}

function WasmTrace_Close(Id) {
  var S = Module["ueTrace"] && Module["ueTrace"][Id];
  if (!S) {
    return;
  }
  S.State = 0;
  S.Closing = true;
  if (!S.Ws || S.Ws.readyState > 1) {
    delete Module["ueTrace"][Id];
  } else if (S.Ws.readyState === 1 && !S.Timer) {
    Module["ueTracePump"](Id);
  }
}

function WasmIoWorker_Create(Ready) {
  function IoWorkerMain() {
    var Memory = null;
    var Utf8 = function(Address) {
      var Bytes = new Uint8Array(Memory.buffer);
      var End = Address;
      while (Bytes[End]) ++End;
      return (new TextDecoder).decode(Bytes.slice(Address, End));
    };
    var FetchRanges = function(Request) {
      var Spec = new Float64Array(Memory.buffer, Request.Ranges, Request.NumRanges * 5).slice();
      var Left = Request.NumRanges;
      var Settle = function(Index, bOk) {
        new Int32Array(Memory.buffer, Request.Results, Request.NumRanges)[Index] = bOk ? 1 : -1;
        if (--Left === 0) {
          var Flag = new Int32Array(Memory.buffer, Request.State, 1);
          Atomics.store(Flag, 0, 1);
          Atomics.notify(Flag, 0);
        }
      };
      var Start = function(Index) {
        var Url = Utf8(Spec[Index * 5]);
        var Offset = Spec[Index * 5 + 1], Size = Spec[Index * 5 + 2], First = Spec[Index * 5 + 3], Count = Spec[Index * 5 + 4];
        fetch(Url, {
          headers: {
            "Range": "bytes=" + Offset + "-" + (Offset + Size - 1)
          }
        }).then(function(Response) {
          if (Response.status !== 206) {
            if (Response.body) Response.body.cancel();
            throw new Error("status " + Response.status);
          }
          return Response.arrayBuffer();
        }).then(function(Data) {
          if (Data.byteLength !== Size) throw new Error("got " + Data.byteLength + " of " + Size + " bytes");
          if (Atomics.load(new Int32Array(Memory.buffer, Request.Generation, 1), 0) !== Request.Expected) return Settle(Index, false);
          var Targets = new Float64Array(Memory.buffer, Request.Dests, (First + Count) * 2);
          var Heap = new Uint8Array(Memory.buffer);
          var At = 0;
          for (var Dest = First; Dest < First + Count; ++Dest) {
            var DestSize = Targets[Dest * 2 + 1];
            Heap.set(new Uint8Array(Data, At, DestSize), Targets[Dest * 2]);
            At += DestSize;
          }
          Settle(Index, true);
        }).catch(function(Failure) {
          console.warn("LogIoDispatcher: range read " + Url + " " + Offset + "+" + Size + " failed (" + Failure + "); read from the file");
          Settle(Index, false);
        });
      };
      for (var Index = 0; Index < Request.NumRanges; ++Index) Start(Index);
    };
    var Files = {};
    var Fail = function(Status) {
      return {
        Status
      };
    };
    var ContentRange = function(Response, Start) {
      var Value = Response.headers.get("Content-Range") || "";
      var Prefix = "bytes " + Start + "-";
      var Slash = Value.lastIndexOf("/");
      return (Value.indexOf(Prefix) === 0 && Slash > Prefix.length) ? parseInt(Value.slice(Slash + 1), 10) : NaN;
    };
    var FirstFetch = async function(Url, ChunkSize) {
      var SizeOnly = Url.split(/[?#]/)[0].toLowerCase().endsWith(".ucas");
      var First = await fetch(Url, {
        headers: {
          "Range": "bytes=0-" + (SizeOnly ? 0 : ChunkSize * 2 - 1)
        }
      });
      if (First.status === 416) return {
        Size: 0,
        Chunks: [ new Uint8Array(0) ],
        ChunkSize: 1
      };
      if (!First.ok) throw Fail(First.status);
      var Data = new Uint8Array(await First.arrayBuffer());
      var Total = First.status === 206 ? ContentRange(First, 0) : Data.byteLength;
      if (isNaN(Total)) {
        console.error("WasmFS fetch: " + Url + " asked bytes 0-" + (ChunkSize * 2 - 1) + ", got " + First.headers.get("Content-Range"));
        throw Fail(0);
      }
      if (Total <= Data.byteLength) return {
        Size: Data.byteLength,
        Chunks: [ Data ],
        ChunkSize: Math.max(1, Data.byteLength)
      };
      if (SizeOnly) return {
        Size: Total,
        Chunks: [],
        ChunkSize
      };
      var Expected = Math.min(ChunkSize * 2, Total);
      if (Data.byteLength < Expected && Data.byteLength < ChunkSize) {
        console.error("WasmFS fetch: " + Url + " asked bytes 0-" + (Expected - 1) + ", got " + Data.byteLength + " bytes");
        throw Fail(0);
      }
      var Chunks = [ Data.slice(0, ChunkSize) ];
      if (Data.byteLength >= Expected) Chunks.push(Data.slice(ChunkSize, Expected));
      return {
        Size: Total,
        Chunks,
        ChunkSize
      };
    };
    var FileInfo = function(Url, ChunkSize) {
      if (!Files[Url]) {
        Files[Url] = FirstFetch(Url, ChunkSize).then(function(Info) {
          Files[Url] = Info;
          return Info;
        }, function(Failure) {
          delete Files[Url];
          throw Failure;
        });
      }
      return Promise.resolve(Files[Url]);
    };
    var FetchChunks = async function(Url, Info, Offset, Length) {
      Length = Math.min(Length, Info.Size - Offset);
      if (Length <= 0) return;
      var ChunkSize = Info.ChunkSize;
      var FirstChunk = Math.floor(Offset / ChunkSize), LastChunk = Math.floor((Offset + Length - 1) / ChunkSize);
      var Missing = false;
      for (var Chunk = FirstChunk; Chunk <= LastChunk && !Missing; ++Chunk) Missing = !Info.Chunks[Chunk];
      if (!Missing) return;
      var Start = FirstChunk * ChunkSize, End = Math.min((LastChunk + 1) * ChunkSize, Info.Size);
      var Response = await fetch(Url, {
        headers: {
          "Range": "bytes=" + Start + "-" + (End - 1)
        }
      });
      if (!Response.ok) throw Fail(Response.status);
      var Bytes = new Uint8Array(await Response.arrayBuffer());
      var bPartial = Response.status === 206;
      if (bPartial ? (isNaN(ContentRange(Response, Start)) || Bytes.byteLength < End - Start) : !(Response.status === 200 && Bytes.byteLength === Info.Size)) {
        console.error("WasmFS fetch: " + Url + " asked bytes " + Start + "-" + (End - 1) + ", got status " + Response.status + " " + Response.headers.get("Content-Range") + " (" + Bytes.byteLength + " bytes)");
        throw Fail(0);
      }
      if (!bPartial) Bytes = Bytes.subarray(Start, End);
      for (Chunk = FirstChunk; Chunk <= LastChunk; ++Chunk) {
        Info.Chunks[Chunk] = Bytes.slice(Chunk * ChunkSize - Start, (Chunk + 1) * ChunkSize - Start);
      }
    };
    var Answer = function(Reply, Result) {
      new Float64Array(Memory.buffer, Reply, 1)[0] = Result;
      var Flag = new Int32Array(Memory.buffer, Reply + 8, 1);
      Atomics.store(Flag, 0, 1);
      Atomics.notify(Flag, 0);
    };
    var ReadFile = async function(Request) {
      try {
        var Info = await FileInfo(Request.Url, Request.ChunkSize);
        await FetchChunks(Request.Url, Info, Request.Offset, Request.Length);
        var Length = Math.min(Request.Length, Info.Size - Request.Offset);
        var Heap = new Uint8Array(Memory.buffer);
        var ChunkSize = Info.ChunkSize;
        for (var Chunk = Math.floor(Request.Offset / ChunkSize); Length > 0 && Chunk * ChunkSize < Request.Offset + Length; ++Chunk) {
          var ChunkStart = Chunk * ChunkSize;
          var Start = Math.max(ChunkStart, Request.Offset), End = Math.min(ChunkStart + ChunkSize, Request.Offset + Length);
          Heap.set(Info.Chunks[Chunk].subarray(Start - ChunkStart, End - ChunkStart), Request.Dest + (Start - Request.Offset));
        }
        Answer(Request.Reply, Math.max(0, Length));
      } catch (Failure) {
        Answer(Request.Reply, Failure && Failure.Status === 404 ? -1 : -2);
      }
    };
    var FileSize = async function(Request) {
      try {
        Answer(Request.Reply, (await FileInfo(Request.Url, Request.ChunkSize)).Size);
      } catch (Failure) {
        Answer(Request.Reply, -2);
      }
    };
    onmessage = function(Init) {
      Memory = Init.data.Memory;
      var Channel = new BroadcastChannel(Init.data.Channel);
      Channel.onmessage = function(Event) {
        if (Event.data.Kind === "ranges") FetchRanges(Event.data); else if (Event.data.Kind === "read") ReadFile(Event.data); else if (Event.data.Kind === "size") FileSize(Event.data);
      };
      var Flag = new Int32Array(Memory.buffer, Init.data.Ready, 1);
      Atomics.store(Flag, 0, 1);
      Atomics.notify(Flag, 0);
    };
  }
  if (typeof wasmMemory === "undefined" || typeof Worker === "undefined" || typeof BroadcastChannel === "undefined") {
    return 0;
  }
  try {
    var ChannelNumber = 1 + Math.floor(Math.random() * 2147483646);
    var Source = "(" + IoWorkerMain.toString() + ")();";
    var IoWorker = new Worker(URL.createObjectURL(new Blob([ Source ], {
      type: "text/javascript"
    })), {
      name: "ue-io"
    });
    IoWorker.postMessage({
      Memory: wasmMemory,
      Channel: "ue-io-" + ChannelNumber,
      Ready
    });
    Module["ueIoWorker"] = IoWorker;
    return ChannelNumber;
  } catch (Failure) {
    console.warn("LogWasm: the IO worker could not start (" + Failure + ")");
    return 0;
  }
}

function WasmIoWorker_PostRanges(Channel, Ranges, NumRanges, Dests, Results, State, Generation, Expected) {
  var Name = "ue-io-" + Channel;
  if (!globalThis.ueIoChannel || globalThis.ueIoChannel.name !== Name) {
    globalThis.ueIoChannel = new BroadcastChannel(Name);
  }
  globalThis.ueIoChannel.postMessage({
    Kind: "ranges",
    Ranges,
    NumRanges,
    Dests,
    Results,
    State,
    Generation,
    Expected
  });
}

function WasmIoWorker_PostFile(Channel, bRead, Url, ChunkSize, Offset, Length, Dest, Reply) {
  var Name = "ue-io-" + Channel;
  if (!globalThis.ueIoChannel || globalThis.ueIoChannel.name !== Name) {
    globalThis.ueIoChannel = new BroadcastChannel(Name);
  }
  globalThis.ueIoChannel.postMessage({
    Kind: bRead ? "read" : "size",
    Url: UTF8ToString(Url),
    ChunkSize,
    Offset,
    Length,
    Dest,
    Reply
  });
}

function WasmPlatformMisc_TouchScreen() {
  var Touch = typeof navigator !== "undefined" && navigator.maxTouchPoints > 0;
  var Coarse = typeof matchMedia === "function" && matchMedia("(pointer: coarse)").matches;
  return (Touch ? 1 : 0) | (Touch && Coarse ? 2 : 0);
}

function WasmPlatformMisc_BroadcastLine(Line) {
  try {
    if (typeof BroadcastChannel === "undefined") {
      return;
    }
    if (!globalThis.__ueLogChannel) {
      globalThis.__ueLogChannel = new BroadcastChannel("ue-log");
    }
    globalThis.__ueLogChannel.postMessage(UTF8ToString(Line));
  } catch (Error) {}
}

function WasmRegex_Install() {
  if (globalThis.__ueRegex) {
    return;
  }
  var LT = "\\n\\x0B\\f\\r\\u0085\\u2028\\u2029";
  var W = "\\p{Alphabetic}\\p{M}\\p{Nd}\\p{Pc}\\u200C\\u200D";
  var WC = "[" + W + "]";
  var SP = "\\p{White_Space}";
  var EndBeforeTerminator = "(?:(?![\\s\\S])|(?=\\r\\n(?![\\s\\S]))|(?<!\\r)(?=\\n(?![\\s\\S]))|(?=[\\x0B\\f\\r\\u0085\\u2028\\u2029](?![\\s\\S])))";
  var StartOfLine = "(?:(?<![\\s\\S])|(?<=[" + LT + "])(?<!\\r(?=\\n))(?=[\\s\\S]))";
  var Grapheme = "(?:\\r\\n|[\\p{Cc}\\p{Zl}\\p{Zp}]|(?:\\p{Regional_Indicator}\\p{Regional_Indicator}" + "|\\p{Extended_Pictographic}(?:[\\p{Grapheme_Extend}\\p{Emoji_Modifier}]*\\u200D\\p{Extended_Pictographic})*" + "|[\\u1100-\\u115F\\uA960-\\uA97C]*(?:[\\uAC00-\\uD7A3]|[\\u1160-\\u11A7\\uD7B0-\\uD7C6])[\\u1160-\\u11A7\\uD7B0-\\uD7C6]*[\\u11A8-\\u11FF\\uD7CB-\\uD7FB]*" + "|[\\u1100-\\u115F\\uA960-\\uA97C]+|[\\u11A8-\\u11FF\\uD7CB-\\uD7FB]+|[^\\p{Cc}\\p{Zl}\\p{Zp}])[\\p{Grapheme_Extend}\\p{Emoji_Modifier}\\p{Mc}\\u200D]*)";
  var EndOfLine = "(?:(?=[\\x0B\\f\\r\\u0085\\u2028\\u2029])|(?<!\\r)(?=\\n)|(?![\\s\\S]))";
  var Posix = {
    alpha: "\\p{Alphabetic}",
    digit: "\\p{Nd}",
    alnum: "[\\p{Alphabetic}\\p{Nd}]",
    upper: "\\p{Uppercase}",
    lower: "\\p{Lowercase}",
    space: "\\p{White_Space}",
    blank: "[\\t\\p{Zs}]",
    punct: "\\p{P}",
    cntrl: "\\p{Cc}",
    xdigit: "[\\p{Nd}\\p{Hex_Digit}]",
    word: WC,
    graph: "[^\\p{White_Space}\\p{Cc}\\p{Cs}\\p{Cn}]",
    print: "[[[^\\p{White_Space}\\p{Cc}\\p{Cs}\\p{Cn}][\\t\\p{Zs}]]--\\p{Cc}]"
  };
  var OPEN = String.fromCharCode(57344), REF = String.fromCharCode(57345), END = String.fromCharCode(57346);
  var Markers = new RegExp("[\\uE000\\uE001]([^\\uE002]*)\\uE002", "g");
  var Cache = new Map;
  var Properties = new Map;
  var TextId = -1, Text = "", SliceBegin = -1, SliceEnd = -1, Slice = "";
  function Fail(What) {
    throw new SyntaxError(What);
  }
  function Hex(C) {
    return "\\u{" + C.toString(16) + "}";
  }
  function IsDigits(S) {
    if (!S.length) {
      return false;
    }
    for (var I = 0; I < S.length; ++I) {
      var C = S.charCodeAt(I);
      if (C < 48 || C > 57) {
        return false;
      }
    }
    return true;
  }
  function IsAlnum(C) {
    return (C >= 48 && C <= 57) || (C >= 65 && C <= 90) || (C >= 97 && C <= 122);
  }
  function Lit(C) {
    return (IsAlnum(C) || (C >= 128 && (C < 55296 || C > 57343) && (C < 57344 || C > 57346))) ? String.fromCodePoint(C) : Hex(C);
  }
  function Property(Name, Neg) {
    var Known = Properties.get(Name);
    if (Known === undefined) {
      Known = Name;
      try {
        new RegExp("\\p{" + Name + "}", "v");
      } catch (E1) {
        Known = "Script=" + Name;
      }
      Properties.set(Name, Known);
    }
    return (Neg ? "\\P{" : "\\p{") + Known + "}";
  }
  function Translate(Src, CaseInsensitive, TotalGroups, CountOnly) {
    var Pos = 0, Len = Src.length, Out = "", Groups = 0, Names = [], Slots = [], AtomStart = 0;
    var Flags = "dgv" + (CaseInsensitive ? "i" : "");
    var Scope = {
      S: false,
      X: false,
      M: false,
      Close: 0,
      Reopen: ""
    };
    var Stack = [];
    function Code() {
      return Pos < Len ? Src.charCodeAt(Pos) : -1;
    }
    function Next() {
      var C = Src.codePointAt(Pos);
      Pos += C > 65535 ? 2 : 1;
      return C;
    }
    function IsSpace(C) {
      return C === 32 || (C >= 9 && C <= 13) || C === 133 || C === 8232 || C === 8233;
    }
    function SkipFree(InSet) {
      while (Scope.X && Pos < Len) {
        var C = Code();
        if (IsSpace(C)) {
          ++Pos;
        } else if (C === 35 && !InSet) {
          while (Pos < Len && Code() !== 10 && Code() !== 13) {
            ++Pos;
          }
        } else {
          break;
        }
      }
    }
    function Push(Start) {
      Stack.push(Scope);
      Scope = {
        S: Scope.S,
        X: Scope.X,
        M: Scope.M,
        Close: 0,
        Reopen: "",
        Start,
        Atomic: -1
      };
    }
    function NewGroup(Name, User) {
      Slots.push({
        Name,
        User
      });
      return OPEN + (Slots.length - 1) + END;
    }
    function Atomic(Body) {
      var Id = Slots.length;
      return "(?:(?=" + NewGroup(null, 0) + Body + "))" + REF + Id + END + ")";
    }
    function HexDigits(Max) {
      var V = 0, N = 0;
      while (N < Max && Pos < Len) {
        var D = parseInt(Src.charAt(Pos), 16);
        if (isNaN(D)) {
          break;
        }
        V = V * 16 + D;
        ++Pos;
        ++N;
      }
      if (N === 0) {
        Fail("bad hex escape");
      }
      return {
        V,
        N
      };
    }
    function HexEscape(Kind) {
      var R;
      if (Kind === 120 && Code() === 123) {
        ++Pos;
        R = HexDigits(8);
        if (Code() !== 125) {
          Fail("bad hex escape");
        }
        ++Pos;
      } else {
        var Need = Kind === 120 ? 2 : (Kind === 117 ? 4 : 8);
        R = HexDigits(Need);
        if (Kind !== 120 && R.N !== Need) {
          Fail("bad hex escape");
        }
      }
      if (R.V > 1114111) {
        Fail("bad code point");
      }
      return R.V;
    }
    function Escape(InSet) {
      if (Pos >= Len) {
        Fail("trailing backslash");
      }
      var C = Next();
      switch (C) {
       case 100:
        return {
          Set: "\\p{Nd}"
        };

       case 68:
        return {
          Set: "\\P{Nd}"
        };

       case 119:
        return {
          Set: WC
        };

       case 87:
        return {
          Set: "[^" + W + "]"
        };

       case 115:
        return {
          Set: "[" + SP + "]"
        };

       case 83:
        return {
          Set: "[^" + SP + "]"
        };

       case 104:
        return {
          Set: "[\\t\\p{Zs}]"
        };

       case 72:
        return {
          Set: "[^\\t\\p{Zs}]"
        };

       case 118:
        return {
          Set: "[" + LT + "]"
        };

       case 86:
        return {
          Set: "[^" + LT + "]"
        };

       case 112:
       case 80:
        {
          var Name;
          if (Code() === 123) {
            var Close = Src.indexOf("}", Pos);
            if (Close < 0) {
              Fail("bad property");
            }
            Name = Src.substring(Pos + 1, Close);
            Pos = Close + 1;
          } else {
            if (Pos >= Len) {
              Fail("bad property");
            }
            Name = String.fromCodePoint(Next());
          }
          var Neg = C === 80;
          if (Name.charAt(0) === "^") {
            Neg = !Neg;
            Name = Name.substring(1);
          }
          return {
            Set: Property(Name, Neg)
          };
        }

       case 110:
        return {
          Char: 10
        };

       case 114:
        return {
          Char: 13
        };

       case 116:
        return {
          Char: 9
        };

       case 102:
        return {
          Char: 12
        };

       case 97:
        return {
          Char: 7
        };

       case 101:
        return {
          Char: 27
        };

       case 99:
        if (Pos >= Len) {
          Fail("bad control escape");
        }
        return {
          Char: Next() & 31
        };

       case 120:
       case 117:
       case 85:
        return {
          Char: HexEscape(C)
        };

       case 48:
        {
          var V = 0, N = 0;
          while (N < 3 && Pos < Len) {
            var D = Code() - 48;
            if (D < 0 || D > 7 || V * 8 + D > 255) {
              break;
            }
            V = V * 8 + D;
            ++Pos;
            ++N;
          }
          if (N === 0) {
            Fail("bad octal escape");
          }
          return {
            Char: V
          };
        }

       case 81:
        {
          var End = Src.indexOf("\\E", Pos);
          var Quoted = Src.substring(Pos, End < 0 ? Len : End);
          Pos = End < 0 ? Len : End + 2;
          return {
            Quote: Quoted
          };
        }

       case 69:
        return {
          Quote: ""
        };

       case 78:
        Fail("named characters are not supported");
      }
      if (InSet) {
        if (C === 98) {
          return {
            Char: 8
          };
        }
      } else {
        switch (C) {
         case 98:
          return {
            Atom: "(?:(?<=" + WC + ")(?!" + WC + ")|(?<!" + WC + ")(?=" + WC + "))"
          };

         case 66:
          return {
            Atom: "(?:(?<=" + WC + ")(?=" + WC + ")|(?<!" + WC + ")(?!" + WC + "))"
          };

         case 65:
          return {
            Atom: "(?<![\\s\\S])"
          };

         case 122:
          return {
            Atom: "(?![\\s\\S])"
          };

         case 90:
          return {
            Atom: EndBeforeTerminator
          };

         case 82:
          return {
            Atom: "(?:\\r\\n|\\r(?!\\n)|[\\n\\x0B\\f\\u0085\\u2028\\u2029])"
          };

         case 88:
          return {
            Atom: Atomic(Grapheme)
          };

         case 107:
          {
            var Gt = Src.indexOf(">", Pos);
            if (Code() !== 60 || Gt < 0) {
              Fail("bad named back reference");
            }
            var Ref = Src.substring(Pos + 1, Gt);
            Pos = Gt + 1;
            return {
              Atom: "\\k<" + Ref + ">"
            };
          }
        }
        if (C >= 49 && C <= 57) {
          var G = C - 48;
          while (Pos < Len) {
            var D2 = Code() - 48;
            if (D2 < 0 || D2 > 9 || G * 10 + D2 > TotalGroups) {
              break;
            }
            G = G * 10 + D2;
            ++Pos;
          }
          return {
            Atom: "(?:" + REF + "u" + G + END + ")"
          };
        }
      }
      if (C >= 48 && C <= 57) {
        Fail("bad escape");
      }
      return {
        Char: C
      };
    }
    function Quantifier() {
      SkipFree(false);
      var C = Code();
      if (C === 42 || C === 43 || C === 63) {
        Out += Src.charAt(Pos);
        ++Pos;
      } else if (C === 123) {
        var Close = Src.indexOf("}", Pos);
        var Body = Close < 0 ? "" : Src.substring(Pos + 1, Close);
        var Comma = Body.indexOf(",");
        var Lo = Comma < 0 ? Body : Body.substring(0, Comma), Hi = Comma < 0 ? "0" : (Body.substring(Comma + 1) || "0");
        if (!IsDigits(Lo) || !IsDigits(Hi)) {
          Fail("bad interval");
        }
        Out += "{" + Body + "}";
        Pos = Close + 1;
      } else {
        return;
      }
      C = Code();
      if (C === 63) {
        Out += "?";
        ++Pos;
      } else if (C === 43) {
        ++Pos;
        Out = Out.substring(0, AtomStart) + Atomic(Out.substring(AtomStart));
      }
    }
    function ParseSet() {
      var Neg = false;
      if (Code() === 94) {
        Neg = true;
        ++Pos;
      }
      var Operands = [], Ops = [], Items = [], First = true;
      function Single(Lo) {
        if (Code() === 45 && Pos + 1 < Len && Src.charCodeAt(Pos + 1) !== 93 && Src.charCodeAt(Pos + 1) !== 45 && Src.charCodeAt(Pos + 1) !== 91) {
          ++Pos;
          var Hi;
          if (Code() === 92) {
            ++Pos;
            var E = Escape(true);
            if (E.Char === undefined) {
              Fail("bad range");
            }
            Hi = E.Char;
          } else {
            Hi = Next();
          }
          if (Hi < Lo) {
            Fail("bad range");
          }
          Items.push(Lit(Lo) + "-" + Lit(Hi));
        } else {
          Items.push(Lit(Lo));
        }
      }
      for (;;) {
        SkipFree(true);
        if (Pos >= Len) {
          Fail("unclosed set");
        }
        var C = Code(), C2 = Pos + 1 < Len ? Src.charCodeAt(Pos + 1) : -1;
        if (C === 93 && !First) {
          ++Pos;
          break;
        }
        First = false;
        if ((C === 38 && C2 === 38) || (C === 45 && C2 === 45)) {
          Operands.push(Items);
          Ops.push(C === 38 ? "&&" : "--");
          Items = [];
          Pos += 2;
          continue;
        }
        if ((C === 38 || C === 45) && C2 === 91 && Items.length) {
          Operands.push(Items);
          Ops.push(C === 38 ? "&&" : "--");
          Items = [];
          ++Pos;
          continue;
        }
        if (C === 91) {
          if (C2 === 58) {
            var End = Src.indexOf(":]", Pos + 2);
            if (End < 0) {
              Fail("bad POSIX class");
            }
            var Name = Src.substring(Pos + 2, End), NegP = Name.charAt(0) === "^";
            if (NegP) {
              Name = Name.substring(1);
            }
            var PosixSet = Posix[Name.toLowerCase()];
            if (PosixSet === undefined) {
              Fail("unknown POSIX class");
            }
            Items.push(NegP ? "[^" + PosixSet + "]" : PosixSet);
            Pos = End + 2;
          } else {
            ++Pos;
            Items.push(ParseSet());
          }
          continue;
        }
        if (C === 92) {
          ++Pos;
          var Esc = Escape(true);
          if (Esc.Quote !== undefined) {
            for (var Q = 0; Q < Esc.Quote.length; ) {
              var QC = Esc.Quote.codePointAt(Q);
              Q += QC > 65535 ? 2 : 1;
              Items.push(Lit(QC));
            }
          } else if (Esc.Set !== undefined) {
            Items.push(Esc.Set);
          } else {
            Single(Esc.Char);
          }
          continue;
        }
        Single(Next());
      }
      Operands.push(Items);
      for (var K = 0; K < Operands.length; ++K) {
        if (!Operands[K].length) {
          Fail("empty set");
        }
      }
      if (!Ops.length) {
        return "[" + (Neg ? "^" : "") + Operands[0].join("") + "]";
      }
      var Acc = "[" + Operands[0].join("") + "]";
      for (var O = 0; O < Ops.length; ++O) {
        Acc = "[" + Acc + Ops[O] + "[" + Operands[O + 1].join("") + "]]";
      }
      return Neg ? "[^" + Acc + "]" : Acc;
    }
    function InlineFlags() {
      var On = "", Off = "", Minus = false;
      for (;;) {
        if (Pos >= Len) {
          Fail("unclosed group");
        }
        var C = Src.charAt(Pos);
        if (C === "-") {
          if (Minus) {
            Fail("bad flags");
          }
          Minus = true;
          ++Pos;
          continue;
        }
        if (C === "w") {
          Fail("the w flag is not supported");
        }
        if ("ismx".indexOf(C) >= 0) {
          if (Minus) {
            Off += C;
          } else {
            On += C;
          }
          ++Pos;
          continue;
        }
        if (C === ")" || C === ":") {
          ++Pos;
          return {
            On,
            Off,
            Scoped: C === ":"
          };
        }
        Fail("bad flags");
      }
    }
    function Apply(Target, F) {
      if (F.On.indexOf("s") >= 0) {
        Target.S = true;
      }
      if (F.Off.indexOf("s") >= 0) {
        Target.S = false;
      }
      if (F.On.indexOf("x") >= 0) {
        Target.X = true;
      }
      if (F.Off.indexOf("x") >= 0) {
        Target.X = false;
      }
      if (F.On.indexOf("m") >= 0) {
        Target.M = true;
      }
      if (F.Off.indexOf("m") >= 0) {
        Target.M = false;
      }
    }
    function CaseModifier(F) {
      var I = F.On.indexOf("i") >= 0, NotI = F.Off.indexOf("i") >= 0;
      return (I || NotI) ? "(?" + (I ? "i" : "") + (NotI ? "-i" : "") + ":" : "";
    }
    function Group() {
      var Start = Out.length;
      if (Code() !== 63) {
        ++Groups;
        Out += NewGroup(null, Groups);
        Push(Start);
        return;
      }
      ++Pos;
      var C = Code(), C2 = Pos + 1 < Len ? Src.charCodeAt(Pos + 1) : -1;
      if (C === 58 || C === 61 || C === 33) {
        Out += "(?" + Src.charAt(Pos);
        ++Pos;
        Push(Start);
        return;
      }
      if (C === 62) {
        var Id = Slots.length;
        Out += "(?:(?=" + NewGroup(null, 0);
        ++Pos;
        Push(Start);
        Scope.Atomic = Id;
        return;
      }
      if (C === 35) {
        var End = Src.indexOf(")", Pos);
        if (End < 0) {
          Fail("unclosed comment");
        }
        Pos = End + 1;
        return;
      }
      if (C === 60 && (C2 === 61 || C2 === 33)) {
        Out += "(?<" + Src.charAt(Pos + 1);
        Pos += 2;
        Push(Start);
        return;
      }
      if (C === 60) {
        var Gt = Src.indexOf(">", Pos);
        if (Gt < 0) {
          Fail("bad group name");
        }
        var Name = Src.substring(Pos + 1, Gt);
        ++Groups;
        Names.push([ Name, Groups ]);
        Out += NewGroup(Name, Groups);
        Pos = Gt + 1;
        Push(Start);
        return;
      }
      var F = InlineFlags(), Mod = CaseModifier(F);
      if (F.Scoped) {
        Out += Mod || "(?:";
        Push(Start);
        Apply(Scope, F);
        return;
      }
      Apply(Scope, F);
      if (!Mod) {
        return;
      }
      if (!Stack.length && !Out.length && !Scope.Close) {
        if (F.On.indexOf("i") >= 0) {
          if (Flags.indexOf("i") < 0) {
            Flags += "i";
          }
        } else {
          Flags = Flags.replace("i", "");
        }
        return;
      }
      Out += Mod;
      Scope.Close++;
      Scope.Reopen += Mod;
    }
    for (;;) {
      SkipFree(false);
      if (Pos >= Len) {
        break;
      }
      var C = Next();
      switch (C) {
       case 40:
        Group();
        break;

       case 41:
        if (!Stack.length) {
          Fail("unmatched )");
        }
        Out += ")".repeat(Scope.Close) + ")" + (Scope.Atomic >= 0 ? ")" + REF + Scope.Atomic + END + ")" : "");
        AtomStart = Scope.Start;
        Scope = Stack.pop();
        Quantifier();
        break;

       case 124:
        Out += ")".repeat(Scope.Close) + "|" + Scope.Reopen;
        break;

       case 91:
        AtomStart = Out.length;
        Out += ParseSet();
        Quantifier();
        break;

       case 46:
        AtomStart = Out.length;
        Out += Scope.S ? "[\\s\\S]" : "[^" + LT + "]";
        Quantifier();
        break;

       case 94:
        Out += Scope.M ? StartOfLine : "^";
        break;

       case 36:
        Out += Scope.M ? EndOfLine : EndBeforeTerminator;
        break;

       case 42:
       case 43:
       case 63:
       case 123:
        Fail("nothing to repeat");

       case 92:
        {
          var E = Escape(false);
          if (E.Quote !== undefined) {
            for (var Q = 0; Q < E.Quote.length; ) {
              var QC = E.Quote.codePointAt(Q);
              Q += QC > 65535 ? 2 : 1;
              AtomStart = Out.length;
              Out += Lit(QC);
            }
            if (E.Quote.length) {
              Quantifier();
            }
          } else {
            AtomStart = Out.length;
            Out += E.Atom !== undefined ? E.Atom : (E.Set !== undefined ? (E.Set.charAt(0) === "[" ? E.Set : "[" + E.Set + "]") : Lit(E.Char));
            Quantifier();
          }
          break;
        }

       default:
        AtomStart = Out.length;
        Out += Lit(C);
        Quantifier();
        break;
      }
    }
    if (Stack.length) {
      Fail("unclosed group");
    }
    Out += ")".repeat(Scope.Close);
    if (CountOnly) {
      return {
        Groups
      };
    }
    var Number = [], Count = 0, UserMap = [ 0 ];
    for (var P = Out.indexOf(OPEN); P >= 0; P = Out.indexOf(OPEN, P + 1)) {
      Number[+Out.substring(P + 1, Out.indexOf(END, P))] = ++Count;
    }
    for (var K = 0; K < Slots.length; ++K) {
      if (Slots[K].User) {
        UserMap[Slots[K].User] = Number[K];
      }
    }
    Out = Out.replace(Markers, function(Marker, Body) {
      if (Marker.charAt(0) === OPEN) {
        var Slot = Slots[+Body];
        return Slot.Name !== null ? "(?<" + Slot.Name + ">" : "(";
      }
      var Target = Body.charAt(0) === "u" ? UserMap[+Body.substring(1)] : Number[+Body];
      if (!Target) {
        Fail("bad back reference");
      }
      return "\\" + Target;
    });
    return {
      Source: Out,
      Flags,
      Groups,
      Names,
      Map: UserMap
    };
  }
  function Str(Address, Length) {
    if (Length <= 0) {
      return "";
    }
    var Units = new Uint16Array((growMemViews(), HEAPU8).buffer, Address, Length), S = "";
    for (var I = 0; I < Length; I += 4096) {
      S += String.fromCharCode.apply(null, Units.subarray(I, Math.min(Length, I + 4096)));
    }
    return S;
  }
  function Entry(Source, Flags) {
    var Key = Flags + ":" + Source, E = Cache.get(Key);
    if (E) {
      return E;
    }
    E = {
      Re: null,
      Groups: -1,
      Names: []
    };
    try {
      var CaseInsensitive = (Flags & 1) !== 0;
      var Counted = Translate(Source, CaseInsensitive, 1e9, true);
      var T = Translate(Source, CaseInsensitive, Counted.Groups, false);
      E.Re = new RegExp(T.Source, T.Flags);
      E.Groups = T.Groups;
      E.Names = T.Names;
      E.Map = T.Map;
    } catch (Err) {
      E.Re = null;
    }
    if (Cache.size >= 1024) {
      Cache.clear();
    }
    Cache.set(Key, E);
    return E;
  }
  globalThis.__ueRegex = {
    Compile: function(Source, SourceLength, Flags) {
      var E = Entry(Str(Source, SourceLength), Flags);
      return E.Re ? E.Groups : -1;
    },
    Find: function(Source, SourceLength, Flags, Id, Input, InputLength, Begin, End, From, Out, OutCount) {
      var E = Entry(Str(Source, SourceLength), Flags);
      if (!E.Re) {
        return -2;
      }
      if (TextId !== Id) {
        TextId = Id;
        Text = Str(Input, InputLength);
        SliceBegin = -1;
      }
      if (SliceBegin !== Begin || SliceEnd !== End) {
        SliceBegin = Begin;
        SliceEnd = End;
        Slice = Text.substring(Begin, End);
      }
      E.Re.lastIndex = From - Begin;
      var M = E.Re.exec(Slice);
      if (!M) {
        return -1;
      }
      var Indices = M["indices"], Offsets = new Int32Array((growMemViews(), HEAPU8).buffer, Out, OutCount);
      for (var G = 0; G * 2 + 1 < OutCount; ++G) {
        var R = Indices[G ? E.Map[G] : 0];
        Offsets[G * 2] = R ? R[0] + Begin : -1;
        Offsets[G * 2 + 1] = R ? R[1] + Begin : -1;
      }
      return E.Groups;
    },
    GroupIndex: function(Source, SourceLength, Flags, Name, NameLength) {
      var E = Entry(Str(Source, SourceLength), Flags), Wanted = Str(Name, NameLength);
      for (var I = 0; I < E.Names.length; ++I) {
        if (E.Names[I][0] === Wanted) {
          return E.Names[I][1];
        }
      }
      return -1;
    }
  };
}

function WasmRegex_Compile(Source, SourceLength, Flags) {
  return globalThis.__ueRegex.Compile(Source, SourceLength, Flags);
}

function WasmRegex_Find(Source, SourceLength, Flags, TextId, Input, InputLength, Begin, End, From, Out, OutCount) {
  return globalThis.__ueRegex.Find(Source, SourceLength, Flags, TextId, Input, InputLength, Begin, End, From, Out, OutCount);
}

function WasmRegex_GroupIndex(Source, SourceLength, Flags, Name, NameLength) {
  return globalThis.__ueRegex.GroupIndex(Source, SourceLength, Flags, Name, NameLength);
}

function WasmStartup_IsNode() {
  return (typeof process === "object" && typeof process.versions === "object" && typeof process.versions.node === "string") ? 1 : 0;
}

function WasmStartup_ExitNode(Code) {
  process.exit(Code);
}

function WasmStartup_ExitWeb(Code) {
  out("HARNESS EXIT " + Code);
  if (typeof EXITSTATUS !== "undefined") EXITSTATUS = Code;
  if (typeof PThread !== "undefined") PThread.terminateAllThreads();
  ABORT = true;
  if (typeof Module !== "undefined" && Module["onExit"]) Module["onExit"](Code);
}

function WasmStartup_GetManifestAddress() {
  var Entries = (typeof Module !== "undefined" && Module["wasmManifest"]) ? Module["wasmManifest"] : [];
  var Text = "";
  for (var Index = 0; Index < Entries.length; ++Index) {
    var Entry = Entries[Index];
    if (Entry && Entry.path && Entry.url) {
      Text += Entry.path + "\t" + new URL(Entry.url, (typeof location !== "undefined") ? location.href : "http://localhost/").href + (Entry.late ? "\tlate" : "") + "\n";
    }
  }
  return stringToNewUTF8(Text);
}

function WasmStartup_SavedMode() {
  var Query = (typeof location !== "undefined") ? new URLSearchParams(location.search).get("saved") : null;
  if (Query === "memory") {
    return 1;
  }
  if (Query === "opfs") {
    return 0;
  }
  if (typeof navigator === "undefined") {
    return 0;
  }
  var Agent = navigator.userAgent || "";
  return (/iPhone|iPad|iPod/.test(Agent) || (/Macintosh/.test(Agent) && navigator.maxTouchPoints > 1)) ? 2 : 0;
}

function WasmSockets_Open(Id, SendRing, RecvRing, RelayText, SlotCount, SlotSize) {
  var Sockets = Module["ueSockets"] = Module["ueSockets"] || {};
  var Relay = RelayText ? UTF8ToString(RelayText) : "";
  if (!Relay) {
    Relay = (location.protocol === "https:" ? "wss://" : "ws://") + location.hostname + ":8889";
  } else if (Relay.indexOf("://") < 0) {
    Relay = (location.protocol === "https:" ? "wss://" : "ws://") + Relay;
  }
  Sockets[Id] = {
    Send: SendRing,
    Recv: RecvRing,
    Relay,
    Slots: SlotCount,
    SlotSize,
    Conns: new Map,
    Closed: false
  };
  if (!Module["ueSocketsLogged"]) {
    Module["ueSocketsLogged"] = true;
    console.log("WEB SOCKETS datagrams over the relay at " + Relay);
  }
}

function WasmSockets_Drain(Id) {
  var S = Module["ueSockets"] && Module["ueSockets"][Id];
  if (!S || S.Closed) {
    return;
  }
  var Buffer = (growMemViews(), HEAP8).buffer;
  var State = new Int32Array(Buffer, S.Send, 4);
  var Base = S.Send + 16;
  var Read = Atomics.load(State, 0), Write = Atomics.load(State, 1);
  var Deliver = function(Ip, Port, Bytes) {
    var Beacon = (Ip >>> 0) === 4294967295;
    var Key = (Beacon ? "beacon:" : Ip + ":") + Port;
    var Conn = S.Conns.get(Key);
    if (Conn && Conn.RetryAt) {
      if (Date.now() < Conn.RetryAt) {
        return;
      }
      S.Conns.delete(Key);
      Conn = null;
    }
    if (!Conn) {
      var Address = ((Ip >>> 24) & 255) + "." + ((Ip >>> 16) & 255) + "." + ((Ip >>> 8) & 255) + "." + (Ip & 255);
      Conn = {
        Ip,
        Port,
        Beacon,
        Pending: [],
        Ws: null,
        Failed: false
      };
      S.Conns.set(Key, Conn);
      try {
        Conn.Ws = new WebSocket(S.Relay + (Beacon ? "/beacon?port=" + Port : "/udp?to=" + Address + ":" + Port));
      } catch (Error) {
        console.log("WEB SOCKETS could not open " + S.Relay + " for " + Address + ":" + Port + ": " + Error);
        Conn.RetryAt = Date.now() + 2e3;
        return;
      }
      Conn.Ws.binaryType = "arraybuffer";
      Conn.Ws.onopen = function() {
        console.log("WEB SOCKETS relay connected for " + Address + ":" + Port);
        for (var I = 0; I < Conn.Pending.length; ++I) {
          Conn.Ws.send(Conn.Pending[I]);
        }
        Conn.Pending = [];
      };
      Conn.Ws.onmessage = function(Event) {
        if (S.Closed) {
          return;
        }
        var Data = new Uint8Array(Event.data);
        var FromIp = Conn.Ip >>> 0, FromPort = Conn.Port;
        if (Conn.Beacon) {
          if (Data.length < 6) {
            return;
          }
          var View = new DataView(Event.data);
          FromIp = View.getUint32(0, false);
          FromPort = View.getUint16(4, false);
          Data = Data.subarray(6);
        }
        var RecvState = new Int32Array((growMemViews(), HEAP8).buffer, S.Recv, 4);
        var RRead = Atomics.load(RecvState, 0), RWrite = Atomics.load(RecvState, 1);
        if (((RWrite - RRead) >>> 0) >= S.Slots || Data.length > S.SlotSize - 12) {
          Atomics.add(RecvState, 2, 1);
          return;
        }
        var Slot = S.Recv + 16 + ((RWrite >>> 0) % S.Slots) * S.SlotSize;
        var Header = new Uint32Array((growMemViews(), HEAP8).buffer, Slot, 3);
        Header[0] = Data.length;
        Header[1] = FromIp;
        Header[2] = FromPort;
        new Uint8Array((growMemViews(), HEAP8).buffer, Slot + 12, Data.length).set(Data);
        Atomics.store(RecvState, 1, (RWrite + 1) | 0);
      };
      Conn.Ws.onerror = function() {
        console.log("WEB SOCKETS relay error for " + Address + ":" + Port + " (" + S.Relay + ")");
      };
      Conn.Ws.onclose = function(Event) {
        console.log("WEB SOCKETS relay closed for " + Address + ":" + Port + " (code " + Event.code + "); retrying in 2 s if still in use");
        Conn.RetryAt = Date.now() + 2e3;
        Conn.Pending = [];
      };
    }
    if (Conn.RetryAt || !Conn.Ws) {
      return;
    }
    if (Conn.Ws.readyState === 1) {
      Conn.Ws.send(Bytes);
    } else if (Conn.Ws.readyState === 0) {
      Conn.Pending.push(Bytes);
    }
  };
  while (Read !== Write) {
    var Slot = Base + ((Read >>> 0) % S.Slots) * S.SlotSize;
    var Header = new Uint32Array(Buffer, Slot, 3);
    var Size = Header[0], Ip = Header[1], Port = Header[2];
    Deliver(Ip, Port, new Uint8Array(Buffer, Slot + 12, Size).slice());
    Read = (Read + 1) | 0;
  }
  Atomics.store(State, 0, Read);
}

function WasmSockets_Close(Id) {
  var Sockets = Module["ueSockets"];
  var S = Sockets && Sockets[Id];
  if (!S) {
    return;
  }
  S.Closed = true;
  S.Conns.forEach(function(Conn) {
    if (Conn.Ws) {
      Conn.Ws.onmessage = null;
      Conn.Ws.onclose = null;
      Conn.Ws.onerror = null;
      Conn.Ws.onopen = null;
      try {
        Conn.Ws.close();
      } catch (Error) {}
    }
  });
  delete Sockets[Id];
}

function WasmApplication_CanvasScale() {
  var Canvas = (typeof Module !== "undefined" && Module["canvas"]) ? Module["canvas"] : null;
  return (Canvas && Canvas.clientWidth > 0) ? Canvas.width / Canvas.clientWidth : 1;
}

function WasmApplication_ResizeCanvas(Scale) {
  var Canvas = (typeof Module !== "undefined" && Module["canvas"]) ? Module["canvas"] : null;
  if (!Canvas || Canvas.clientWidth <= 0 || Canvas.clientHeight <= 0) {
    return 0;
  }
  var Width = Math.max(1, Math.round(Canvas.clientWidth * Scale));
  var Height = Math.max(1, Math.round(Canvas.clientHeight * Scale));
  if (Canvas.width !== Width) {
    Canvas.width = Width;
  }
  if (Canvas.height !== Height) {
    Canvas.height = Height;
  }
  return (Width & 65535) | ((Height & 65535) << 16);
}

function WasmApplication_SetPointerCapture(Enable) {
  if (typeof Module === "undefined") {
    return;
  }
  Module["ueWebPageWanted"] = !!Enable;
  var Page = Module["ueWebPage"];
  if (Page) {
    Page.wanted = !!Enable;
  }
  if (Enable) {
    if (Page && Page.lock) {
      Page.lock();
    }
  } else if (document.pointerLockElement) {
    Module["ueWebExitingLock"] = true;
    document.exitPointerLock();
  }
}

function WasmApplication_WatchLockLoss(LostAddress) {
  if (typeof Module === "undefined" || Module["ueWebLockLossWatch"]) {
    return;
  }
  Module["ueWebLockLossWatch"] = true;
  document.addEventListener("pointerlockchange", function() {
    if (document.pointerLockElement) {
      Module["ueWebExitingLock"] = false;
      return;
    }
    var GameEndedIt = !!Module["ueWebExitingLock"];
    Module["ueWebExitingLock"] = false;
    if (!GameEndedIt && Module["ueWebPageWanted"]) {
      Atomics.add((growMemViews(), HEAP32), LostAddress >> 2, 1);
    }
  });
}

function WasmApplication_HoldCanvasTouches() {
  var Canvas = (typeof Module !== "undefined" && Module["canvas"]) ? Module["canvas"] : null;
  if (!Canvas) {
    return;
  }
  var Hold = function(E) {
    if (E.cancelable) {
      E.preventDefault();
    }
  };
  [ "touchstart", "touchmove", "touchend", "touchcancel" ].forEach(function(Type) {
    Canvas.addEventListener(Type, Hold, {
      passive: false
    });
  });
}

function WasmApplication_SetCanvasCursor(Css) {
  var Canvas = (typeof Module !== "undefined" && Module["canvas"]) || document.getElementById("canvas");
  if (Canvas) {
    Canvas.style.cursor = UTF8ToString(Number(Css));
  }
}

function WasmApplication_HasFinePointer() {
  try {
    return (typeof matchMedia !== "function" || matchMedia("(any-pointer: fine)").matches) ? 1 : 0;
  } catch (e) {
    return 1;
  }
}

function WasmGamepad_Start(Snapshot, Pads, Words, Axes, Buttons) {
  var I32 = new Int32Array((growMemViews(), HEAPU8).buffer, Snapshot, 2 + Pads * Words);
  var F32 = new Float32Array((growMemViews(), HEAPU8).buffer, Snapshot, 2 + Pads * Words);
  window.addEventListener("gamepadconnected", function(E) {
    console.log("Gamepad connected: " + E.gamepad.index + " '" + E.gamepad.id + "' mapping '" + E.gamepad.mapping + "'");
  });
  window.addEventListener("gamepaddisconnected", function(E) {
    console.log("Gamepad disconnected: " + E.gamepad.index);
  });
  var WasDown = [];
  for (var P = 0; P < Pads; ++P) {
    WasDown.push(new Array(Buttons).fill(false));
  }
  function Poll() {
    var List = [];
    try {
      List = navigator.getGamepads ? navigator.getGamepads() : [];
    } catch (Error) {}
    Atomics.add(I32, 0, 1);
    try {
      for (var Slot = 0; Slot < Pads; ++Slot) {
        var Pad = Slot < List.length ? List[Slot] : null, W = 2 + Slot * Words;
        if (!Pad || !Pad.connected) {
          I32[W] = 0;
          WasDown[Slot].fill(false);
          continue;
        }
        I32[W] = 1;
        I32[W + 1] = Pad.mapping === "standard" ? 1 : 0;
        for (var A = 0; A < Axes; ++A) {
          F32[W + 2 + A] = A < Pad.axes.length ? Pad.axes[A] : 0;
        }
        for (var B = 0; B < Buttons; ++B) {
          var Button = B < Pad.buttons.length ? Pad.buttons[B] : null;
          var Value = !Button ? 0 : (typeof Button === "number" ? Button : (+Button.value || 0));
          var Pressed = !!(Button && Button.pressed);
          var Out = (B === 6 || B === 7) ? (Value || (Pressed ? 1 : 0)) : ((Pressed || Value > .5) ? 1 : 0);
          F32[W + 2 + Axes + B] = Out;
          var Down = (B === 6 || B === 7) ? Out > 30 / 255 : Out > .5;
          if (Down && !WasDown[Slot][B]) {
            I32[W + 2 + Axes + Buttons + B] += 1;
          }
          WasDown[Slot][B] = Down;
        }
      }
    } finally {
      Atomics.add(I32, 0, 1);
      Atomics.add(I32, 1, 1);
    }
  }
  Module["ueGamepadPoll"] = Poll;
  setInterval(Poll, 8);
}

function WasmGamepad_Poll() {
  if (Module["ueGamepadPoll"]) {
    Module["ueGamepadPoll"]();
  }
}

function WasmPlatformApplicationMisc_CanvasSize() {
  var Canvas = (typeof Module !== "undefined" && Module["canvas"]) ? Module["canvas"] : null;
  return Canvas ? ((Canvas.width & 65535) | ((Canvas.height & 65535) << 16)) : 0;
}

function WebGPUHasPageDevice() {
  return Module["preinitializedWebGPUDevice"] ? 1 : 0;
}

function WebGPUWatchPageDevice() {
  var Device = Module["preinitializedWebGPUDevice"];
  var Count = 0;
  Device.addEventListener("uncapturederror", function(Event) {
    var Err = Event.error;
    var Type = (typeof GPUValidationError !== "undefined" && Err instanceof GPUValidationError) ? 2 : (typeof GPUOutOfMemoryError !== "undefined" && Err instanceof GPUOutOfMemoryError) ? 3 : (typeof GPUInternalError !== "undefined" && Err instanceof GPUInternalError) ? 4 : 5;
    Count++;
    console.error("WEBGPU ERROR " + Count + " type " + Type + ": " + String(Err && Err.message).split(String.fromCharCode(10)).join(" "));
  });
  Device.lost.then(function(Info) {
    if (Info.reason === "destroyed") {
      return;
    }
    var Handled = false;
    try {
      Handled = typeof globalThis["ueOnWebGPUDeviceLost"] === "function" && !!globalThis["ueOnWebGPUDeviceLost"](String(Info.reason), String(Info.message));
    } catch (Error) {
      console.error(Error);
    }
    console.error("WEBGPU ERROR device lost (" + Info.reason + "): " + Info.message + (Handled ? "; the page handles it" : ""));
    if (!Handled && typeof abort === "function") {
      abort("WebGPU device lost (" + Info.reason + "): " + Info.message);
    }
  });
}

function WebGPUCallPageDeviceLost(Reason, Message) {
  if (typeof globalThis["ueOnWebGPUDeviceLost"] !== "function") {
    return 0;
  }
  try {
    return globalThis["ueOnWebGPUDeviceLost"](String(Reason), Message ? UTF8ToString(Number(Message)) : "") ? 1 : 0;
  } catch (Error) {
    console.error(Error);
    return 0;
  }
}

function WebGPUAnswerPageDevice(bTaken, Why) {
  if (typeof window !== "undefined" && window.dispatchEvent) {
    window.dispatchEvent(new CustomEvent("ue-webgpu-device", {
      detail: {
        page: !!bTaken,
        why: Why ? UTF8ToString(Number(Why)) : ""
      }
    }));
  }
}

function WebGPUCallPagePipelineFailed(bCompute, Entry, Message, bDesktop, bDeviceLimit, Tier) {
  if (typeof globalThis["ueOnWebGPUPipelineFailed"] !== "function") {
    return;
  }
  try {
    globalThis["ueOnWebGPUPipelineFailed"](bCompute ? "compute" : "render", Entry ? UTF8ToString(Number(Entry)) : "", Message ? UTF8ToString(Number(Message)) : "", !!bDesktop, !!bDeviceLimit, [ "forward", "deferred", "desktop" ][Tier] || "");
  } catch (Error) {
    console.error(Error);
  }
}

function IasFetch_Start(SlotPtr, SlotIndex, Generation, Method, Url, Headers, BodyPtr, BodyLength) {
  var State = Module.ueIasFetch || (Module.ueIasFetch = {
    live: {},
    cancelled: {}
  });
  var Ints = function() {
    return new Int32Array((growMemViews(), HEAP8).buffer, SlotPtr, 4);
  };
  var Longs = function() {
    return new BigInt64Array((growMemViews(), HEAP8).buffer, SlotPtr + 16, 3);
  };
  var Entry = {
    gen: Generation,
    ctrl: null,
    head: null,
    body: null
  };
  var Fail = function(Kind) {
    if (State.live[SlotIndex] !== Entry) {
      return;
    }
    delete State.live[SlotIndex];
    var I = Ints();
    I[2] = Kind;
    Atomics.store(I, 0, 3);
    Atomics.store(I, 3, 1);
  };
  State.live[SlotIndex] = Entry;
  if (State.cancelled[SlotIndex] === Generation) {
    delete State.cancelled[SlotIndex];
    Fail(2);
    return;
  }
  var UrlText = UTF8ToString(Url);
  var Init = {
    method: UTF8ToString(Method),
    headers: [],
    mode: "cors",
    cache: "no-store",
    redirect: "follow",
    credentials: "same-origin"
  };
  UTF8ToString(Headers).split("\n").forEach(function(Line) {
    var Colon = Line.indexOf(":");
    if (Colon > 0) {
      Init.headers.push([ Line.substring(0, Colon).trim(), Line.substring(Colon + 1).trim() ]);
    }
  });
  if (BodyLength > 0) {
    Init.body = (growMemViews(), HEAPU8).slice(BodyPtr, BodyPtr + BodyLength);
  }
  if (typeof AbortController !== "undefined") {
    Entry.ctrl = new AbortController;
    Init.signal = Entry.ctrl.signal;
  }
  var Pending;
  try {
    Pending = fetch(UrlText, Init);
  } catch (Error) {
    Fail(3);
    return;
  }
  Pending.then(function(Response) {
    if (State.live[SlotIndex] !== Entry) {
      return;
    }
    Ints()[1] = Response.status;
    var Head = (Response.statusText || "") + "\n";
    Response.headers.forEach(function(Value, Name) {
      Head += Name + ": " + Value + "\n";
    });
    var Chunks = [];
    var Total = 0;
    var Finish = function() {
      if (State.live[SlotIndex] !== Entry) {
        return;
      }
      var Body = new Uint8Array(Total);
      var Offset = 0;
      Chunks.forEach(function(Chunk) {
        Body.set(Chunk, Offset);
        Offset += Chunk.length;
      });
      Entry.head = (new TextEncoder).encode(Head);
      Entry.body = Body;
      var L = Longs();
      Atomics.store(L, 0, BigInt(Entry.head.length));
      Atomics.store(L, 1, BigInt(Total));
      Atomics.store(L, 2, BigInt(Total));
      Atomics.store(Ints(), 0, 2);
    };
    if (!Response.body || !Response.body.getReader) {
      return Response.arrayBuffer().then(function(Buffer) {
        Chunks.push(new Uint8Array(Buffer));
        Total = Buffer.byteLength;
        Finish();
      });
    }
    var Reader = Response.body.getReader();
    var Pump = function() {
      return Reader.read().then(function(Part) {
        if (State.live[SlotIndex] !== Entry) {
          Reader.cancel().catch(function() {});
          return;
        }
        if (Part.done) {
          Finish();
          return;
        }
        Chunks.push(Part.value);
        Total += Part.value.length;
        Atomics.store(Longs(), 2, BigInt(Total));
        return Pump();
      });
    };
    return Pump();
  }).catch(function(Error) {
    Fail(Error && Error.name === "AbortError" ? 2 : (Error && Error.name === "TypeError" ? 1 : 3));
  });
}

function IasFetch_Copy(SlotPtr, SlotIndex, Generation, Dest, Size) {
  var State = Module.ueIasFetch;
  var Entry = State && State.live[SlotIndex];
  if (!Entry || Entry.gen !== Generation || !Entry.body) {
    return;
  }
  var I = new Int32Array((growMemViews(), HEAP8).buffer, SlotPtr, 4);
  delete State.live[SlotIndex];
  if (Entry.head.length + Entry.body.length === Size) {
    (growMemViews(), HEAPU8).set(Entry.head, Dest);
    (growMemViews(), HEAPU8).set(Entry.body, Dest + Entry.head.length);
    Atomics.store(I, 0, 4);
  } else {
    I[2] = 3;
    Atomics.store(I, 0, 3);
  }
  Atomics.store(I, 3, 1);
}

function IasFetch_Abort(SlotPtr, SlotIndex, Generation) {
  var State = Module.ueIasFetch || (Module.ueIasFetch = {
    live: {},
    cancelled: {}
  });
  var Entry = State.live[SlotIndex];
  var I = new Int32Array((growMemViews(), HEAP8).buffer, SlotPtr, 4);
  if (Entry && Entry.gen === Generation) {
    delete State.live[SlotIndex];
    if (Entry.ctrl) {
      try {
        Entry.ctrl.abort();
      } catch (Error) {}
    }
    I[2] = 2;
    Atomics.store(I, 0, 3);
    Atomics.store(I, 3, 1);
  } else if (Atomics.load(I, 3) === 0) {
    State.cancelled[SlotIndex] = Generation;
  }
}

function WasmPage_InstallExec(BufferOffset, FlagOffset, Size) {
  if (globalThis.ueExec) {
    return;
  }
  var Queue = [];
  var Drain = function() {
    var Flag = new Int32Array((growMemViews(), HEAP8).buffer, FlagOffset, 1);
    if (Queue.length === 0 || Atomics.load(Flag, 0) !== 0) {
      return;
    }
    var Command = Queue.shift();
    var Bytes = new Uint8Array((growMemViews(), HEAP8).buffer, BufferOffset, Size);
    var Length = 0;
    for (var Index = 0; Index < Command.length && Length < Size - 1; ++Index) {
      var Code = Command.charCodeAt(Index);
      Bytes[Length++] = Code < 128 ? Code : 63;
    }
    Bytes[Length] = 0;
    Atomics.store(Flag, 0, 1);
  };
  globalThis.ueExec = function(Command) {
    Queue.push(String(Command));
    Drain();
    return "queued: " + Command;
  };
  setInterval(Drain, 50);
  console.log('PAGE ueExec ready: window.ueExec("<console command>")');
}

function AudioMixerWebAudio_Create(RingOffset, StateOffset, RingFrames, SampleRate) {
  if (typeof AudioContext === "undefined" || typeof AudioWorkletNode === "undefined") {
    console.log("WebAudio: not available");
    return 0;
  }
  if (typeof navigator !== "undefined" && navigator.audioSession) {
    try {
      navigator.audioSession.type = "playback";
    } catch (Error) {
      console.log("WebAudio: audio session type not set: " + Error);
    }
  }
  var Context;
  try {
    Context = new AudioContext({
      sampleRate: SampleRate,
      latencyHint: "interactive"
    });
  } catch (Error) {
    console.log("WebAudio: no AudioContext: " + Error);
    return 0;
  }
  Module["ueAudioContext"] = Context;
  var Device = {
    Context,
    Node: null,
    Resume: null,
    Closed: false
  };
  (Module["ueAudioDevices"] = Module["ueAudioDevices"] || {})[StateOffset] = Device;
  var Source = [ "class UEFeed extends AudioWorkletProcessor {", "  constructor(Options) {", "    super();", "    var O = Options.processorOptions;", "    this.Ring = new Float32Array(O.Buffer, O.Ring, O.Frames * 2);", "    this.State = new Int32Array(O.Buffer, O.State, 4);", "    this.Mask = O.Frames - 1;", "  }", "  process(Inputs, Outputs) {", "    var Out = Outputs[0], L = Out[0], R = Out.length > 1 ? Out[1] : null, N = L.length;", "    if (Atomics.load(this.State, 3) >= 2) {", "      Atomics.store(this.State, 3, 3); L.fill(0); if (R) { R.fill(0); }", "      return false;", "    }", "    var Read = Atomics.load(this.State, 0) >>> 0, Write = Atomics.load(this.State, 1) >>> 0;", "    if (((Write - Read) >>> 0) < N) {", "      L.fill(0); if (R) { R.fill(0); }", "      if (Atomics.load(this.State, 3) !== 0) { Atomics.add(this.State, 2, 1); }", "      return true;", "    }", "    for (var I = 0; I < N; ++I) {", "      var K = ((Read + I) & this.Mask) * 2;", "      L[I] = this.Ring[K]; if (R) { R[I] = this.Ring[K + 1]; }", "    }", "    Atomics.store(this.State, 0, (Read + N) | 0);", "    return true;", "  }", "}", "registerProcessor('ue-feed', UEFeed);" ].join("\n");
  var Url = URL.createObjectURL(new Blob([ Source ], {
    type: "application/javascript"
  }));
  Context.audioWorklet.addModule(Url).then(function() {
    if (Device.Closed) {
      return;
    }
    var Node = new AudioWorkletNode(Context, "ue-feed", {
      numberOfInputs: 0,
      numberOfOutputs: 1,
      outputChannelCount: [ 2 ],
      processorOptions: {
        Buffer: (growMemViews(), HEAPF32).buffer,
        Ring: RingOffset,
        State: StateOffset,
        Frames: RingFrames
      }
    });
    Node.connect(Context.destination);
    Module["ueAudioNode"] = Node;
    Device.Node = Node;
    console.log("WebAudio: worklet running at " + Context.sampleRate + " Hz, context " + Context.state);
  }).catch(function(Error) {
    console.log("WebAudio: worklet failed: " + Error);
  });
  var Resume = function() {
    if (Context.state !== "running" && !Device.Closed) {
      Context.resume();
    }
  };
  Device.Resume = Resume;
  window.addEventListener("pointerdown", Resume);
  window.addEventListener("keydown", Resume);
  Context.resume();
  return Context.sampleRate | 0;
}

function AudioMixerWebAudio_HasNode(StateOffset) {
  var Devices = Module["ueAudioDevices"];
  var Device = Devices && Devices[StateOffset];
  return (Device && Device.Node && Device.Context.state !== "closed") ? 1 : 0;
}

function AudioMixerWebAudio_Destroy(StateOffset) {
  var Devices = Module["ueAudioDevices"];
  var Device = Devices && Devices[StateOffset];
  if (!Device) {
    return;
  }
  Device.Closed = true;
  window.removeEventListener("pointerdown", Device.Resume);
  window.removeEventListener("keydown", Device.Resume);
  if (Device.Node) {
    try {
      Device.Node.disconnect();
    } catch (Error) {}
    if (Module["ueAudioNode"] === Device.Node) {
      delete Module["ueAudioNode"];
    }
  }
  if (Module["ueAudioContext"] === Device.Context) {
    delete Module["ueAudioContext"];
  }
  Device.Context.close().catch(function() {});
  delete Devices[StateOffset];
  console.log("WebAudio: context closed");
}

// Imports from the Wasm binary.
var _free = makeInvalidEarlyAccess("_free");

var _malloc = makeInvalidEarlyAccess("_malloc");

var _fflush = makeInvalidEarlyAccess("_fflush");

var _pthread_self = makeInvalidEarlyAccess("_pthread_self");

var _wasmfs_create_memory_backend = makeInvalidEarlyAccess("_wasmfs_create_memory_backend");

var _wasmfs_create_file = makeInvalidEarlyAccess("_wasmfs_create_file");

var _main = Module["_main"] = makeInvalidEarlyAccess("_main");

var _PhoenixSessionResponse = Module["_PhoenixSessionResponse"] = makeInvalidEarlyAccess("_PhoenixSessionResponse");

var _PhoenixTransportReceive = Module["_PhoenixTransportReceive"] = makeInvalidEarlyAccess("_PhoenixTransportReceive");

var _PhoenixFleetStorage = Module["_PhoenixFleetStorage"] = makeInvalidEarlyAccess("_PhoenixFleetStorage");

var _PhoenixModDownloaded = Module["_PhoenixModDownloaded"] = makeInvalidEarlyAccess("_PhoenixModDownloaded");

var _PhoenixImportSnapshot = Module["_PhoenixImportSnapshot"] = makeInvalidEarlyAccess("_PhoenixImportSnapshot");

var _PhoenixWebAction = Module["_PhoenixWebAction"] = makeInvalidEarlyAccess("_PhoenixWebAction");

var _PhoenixWebAuthoredAction = Module["_PhoenixWebAuthoredAction"] = makeInvalidEarlyAccess("_PhoenixWebAuthoredAction");

var _PhoenixWebHostAction = Module["_PhoenixWebHostAction"] = makeInvalidEarlyAccess("_PhoenixWebHostAction");

var _emwgpuCreateBindGroup = makeInvalidEarlyAccess("_emwgpuCreateBindGroup");

var _emwgpuCreateBindGroupLayout = makeInvalidEarlyAccess("_emwgpuCreateBindGroupLayout");

var _emwgpuCreateCommandBuffer = makeInvalidEarlyAccess("_emwgpuCreateCommandBuffer");

var _emwgpuCreateCommandEncoder = makeInvalidEarlyAccess("_emwgpuCreateCommandEncoder");

var _emwgpuCreateComputePassEncoder = makeInvalidEarlyAccess("_emwgpuCreateComputePassEncoder");

var _emwgpuCreateComputePipeline = makeInvalidEarlyAccess("_emwgpuCreateComputePipeline");

var _emwgpuCreateExternalTexture = makeInvalidEarlyAccess("_emwgpuCreateExternalTexture");

var _emwgpuCreatePipelineLayout = makeInvalidEarlyAccess("_emwgpuCreatePipelineLayout");

var _emwgpuCreateQuerySet = makeInvalidEarlyAccess("_emwgpuCreateQuerySet");

var _emwgpuCreateRenderBundle = makeInvalidEarlyAccess("_emwgpuCreateRenderBundle");

var _emwgpuCreateRenderBundleEncoder = makeInvalidEarlyAccess("_emwgpuCreateRenderBundleEncoder");

var _emwgpuCreateRenderPassEncoder = makeInvalidEarlyAccess("_emwgpuCreateRenderPassEncoder");

var _emwgpuCreateRenderPipeline = makeInvalidEarlyAccess("_emwgpuCreateRenderPipeline");

var _emwgpuCreateSampler = makeInvalidEarlyAccess("_emwgpuCreateSampler");

var _emwgpuCreateSurface = makeInvalidEarlyAccess("_emwgpuCreateSurface");

var _emwgpuCreateTexture = makeInvalidEarlyAccess("_emwgpuCreateTexture");

var _emwgpuCreateTextureView = makeInvalidEarlyAccess("_emwgpuCreateTextureView");

var _emwgpuCreateAdapter = makeInvalidEarlyAccess("_emwgpuCreateAdapter");

var _emwgpuImportBuffer = makeInvalidEarlyAccess("_emwgpuImportBuffer");

var _emwgpuCreateDevice = makeInvalidEarlyAccess("_emwgpuCreateDevice");

var _emwgpuCreateQueue = makeInvalidEarlyAccess("_emwgpuCreateQueue");

var _emwgpuCreateShaderModule = makeInvalidEarlyAccess("_emwgpuCreateShaderModule");

var _emwgpuOnCompilationInfoCompleted = makeInvalidEarlyAccess("_emwgpuOnCompilationInfoCompleted");

var _emwgpuOnCreateComputePipelineCompleted = makeInvalidEarlyAccess("_emwgpuOnCreateComputePipelineCompleted");

var _emwgpuOnCreateRenderPipelineCompleted = makeInvalidEarlyAccess("_emwgpuOnCreateRenderPipelineCompleted");

var _emwgpuOnDeviceLostCompleted = makeInvalidEarlyAccess("_emwgpuOnDeviceLostCompleted");

var _emwgpuOnMapAsyncCompleted = makeInvalidEarlyAccess("_emwgpuOnMapAsyncCompleted");

var _emwgpuOnPopErrorScopeCompleted = makeInvalidEarlyAccess("_emwgpuOnPopErrorScopeCompleted");

var _emwgpuOnRequestAdapterCompleted = makeInvalidEarlyAccess("_emwgpuOnRequestAdapterCompleted");

var _emwgpuOnRequestDeviceCompleted = makeInvalidEarlyAccess("_emwgpuOnRequestDeviceCompleted");

var _emwgpuOnWorkDoneCompleted = makeInvalidEarlyAccess("_emwgpuOnWorkDoneCompleted");

var _emwgpuOnUncapturedError = makeInvalidEarlyAccess("_emwgpuOnUncapturedError");

var _wgpuDeviceAddRef = makeInvalidEarlyAccess("_wgpuDeviceAddRef");

var __emscripten_tls_init = makeInvalidEarlyAccess("__emscripten_tls_init");

var _emscripten_builtin_memalign = makeInvalidEarlyAccess("_emscripten_builtin_memalign");

var __emscripten_proxy_main = Module["__emscripten_proxy_main"] = makeInvalidEarlyAccess("__emscripten_proxy_main");

var _emscripten_stack_get_base = makeInvalidEarlyAccess("_emscripten_stack_get_base");

var _emscripten_stack_get_end = makeInvalidEarlyAccess("_emscripten_stack_get_end");

var __emscripten_run_callback_on_thread = makeInvalidEarlyAccess("__emscripten_run_callback_on_thread");

var ___funcs_on_exit = makeInvalidEarlyAccess("___funcs_on_exit");

var __emscripten_thread_init = makeInvalidEarlyAccess("__emscripten_thread_init");

var ___set_thread_state = makeInvalidEarlyAccess("___set_thread_state");

var __emscripten_thread_crashed = makeInvalidEarlyAccess("__emscripten_thread_crashed");

var _emscripten_proxy_execute_queue = makeInvalidEarlyAccess("_emscripten_proxy_execute_queue");

var _emscripten_proxy_finish = makeInvalidEarlyAccess("_emscripten_proxy_finish");

var __emscripten_run_js_on_main_thread_done = makeInvalidEarlyAccess("__emscripten_run_js_on_main_thread_done");

var __emscripten_run_js_on_main_thread = makeInvalidEarlyAccess("__emscripten_run_js_on_main_thread");

var __emscripten_thread_free_data = makeInvalidEarlyAccess("__emscripten_thread_free_data");

var __emscripten_thread_exit = makeInvalidEarlyAccess("__emscripten_thread_exit");

var __emscripten_check_mailbox = makeInvalidEarlyAccess("__emscripten_check_mailbox");

var _memalign = makeInvalidEarlyAccess("_memalign");

var _setThrew = makeInvalidEarlyAccess("_setThrew");

var __emscripten_tempret_set = makeInvalidEarlyAccess("__emscripten_tempret_set");

var _emscripten_stack_init = makeInvalidEarlyAccess("_emscripten_stack_init");

var _emscripten_stack_set_limits = makeInvalidEarlyAccess("_emscripten_stack_set_limits");

var _emscripten_stack_get_free = makeInvalidEarlyAccess("_emscripten_stack_get_free");

var __emscripten_stack_restore = makeInvalidEarlyAccess("__emscripten_stack_restore");

var __emscripten_stack_alloc = makeInvalidEarlyAccess("__emscripten_stack_alloc");

var _emscripten_stack_get_current = makeInvalidEarlyAccess("_emscripten_stack_get_current");

var ___cxa_decrement_exception_refcount = makeInvalidEarlyAccess("___cxa_decrement_exception_refcount");

var ___cxa_increment_exception_refcount = makeInvalidEarlyAccess("___cxa_increment_exception_refcount");

var ___get_exception_message = makeInvalidEarlyAccess("___get_exception_message");

var ___cxa_can_catch = makeInvalidEarlyAccess("___cxa_can_catch");

var ___cxa_get_exception_ptr = makeInvalidEarlyAccess("___cxa_get_exception_ptr");

var __wasmfs_fetch_get_file_url = makeInvalidEarlyAccess("__wasmfs_fetch_get_file_url");

var __wasmfs_fetch_get_chunk_size = makeInvalidEarlyAccess("__wasmfs_fetch_get_chunk_size");

var __wasmfs_read_file = makeInvalidEarlyAccess("__wasmfs_read_file");

var __wasmfs_write_file = makeInvalidEarlyAccess("__wasmfs_write_file");

var __wasmfs_mkdir = makeInvalidEarlyAccess("__wasmfs_mkdir");

var __wasmfs_rmdir = makeInvalidEarlyAccess("__wasmfs_rmdir");

var __wasmfs_open = makeInvalidEarlyAccess("__wasmfs_open");

var __wasmfs_mknod = makeInvalidEarlyAccess("__wasmfs_mknod");

var __wasmfs_unlink = makeInvalidEarlyAccess("__wasmfs_unlink");

var __wasmfs_chdir = makeInvalidEarlyAccess("__wasmfs_chdir");

var __wasmfs_symlink = makeInvalidEarlyAccess("__wasmfs_symlink");

var __wasmfs_readlink = makeInvalidEarlyAccess("__wasmfs_readlink");

var __wasmfs_write = makeInvalidEarlyAccess("__wasmfs_write");

var __wasmfs_pwrite = makeInvalidEarlyAccess("__wasmfs_pwrite");

var __wasmfs_chmod = makeInvalidEarlyAccess("__wasmfs_chmod");

var __wasmfs_fchmod = makeInvalidEarlyAccess("__wasmfs_fchmod");

var __wasmfs_lchmod = makeInvalidEarlyAccess("__wasmfs_lchmod");

var __wasmfs_llseek = makeInvalidEarlyAccess("__wasmfs_llseek");

var __wasmfs_rename = makeInvalidEarlyAccess("__wasmfs_rename");

var __wasmfs_read = makeInvalidEarlyAccess("__wasmfs_read");

var __wasmfs_pread = makeInvalidEarlyAccess("__wasmfs_pread");

var __wasmfs_truncate = makeInvalidEarlyAccess("__wasmfs_truncate");

var __wasmfs_ftruncate = makeInvalidEarlyAccess("__wasmfs_ftruncate");

var __wasmfs_close = makeInvalidEarlyAccess("__wasmfs_close");

var __wasmfs_mmap = makeInvalidEarlyAccess("__wasmfs_mmap");

var __wasmfs_msync = makeInvalidEarlyAccess("__wasmfs_msync");

var __wasmfs_munmap = makeInvalidEarlyAccess("__wasmfs_munmap");

var __wasmfs_utime = makeInvalidEarlyAccess("__wasmfs_utime");

var __wasmfs_stat = makeInvalidEarlyAccess("__wasmfs_stat");

var __wasmfs_lstat = makeInvalidEarlyAccess("__wasmfs_lstat");

var __wasmfs_mount = makeInvalidEarlyAccess("__wasmfs_mount");

var __wasmfs_identify = makeInvalidEarlyAccess("__wasmfs_identify");

var __wasmfs_readdir_start = makeInvalidEarlyAccess("__wasmfs_readdir_start");

var __wasmfs_readdir_get = makeInvalidEarlyAccess("__wasmfs_readdir_get");

var __wasmfs_readdir_finish = makeInvalidEarlyAccess("__wasmfs_readdir_finish");

var __wasmfs_get_cwd = makeInvalidEarlyAccess("__wasmfs_get_cwd");

var _wasmfs_create_jsimpl_backend = makeInvalidEarlyAccess("_wasmfs_create_jsimpl_backend");

var __wasmfs_opfs_record_entry = makeInvalidEarlyAccess("__wasmfs_opfs_record_entry");

var _wasmfs_unmount = makeInvalidEarlyAccess("_wasmfs_unmount");

var _wasmfs_flush = makeInvalidEarlyAccess("_wasmfs_flush");

var __indirect_function_table = makeInvalidEarlyAccess("__indirect_function_table");

var _GNameBlocksDebug = Module["_GNameBlocksDebug"] = makeInvalidEarlyAccess("_GNameBlocksDebug");

var _GObjectArrayForDebugVisualizers = Module["_GObjectArrayForDebugVisualizers"] = makeInvalidEarlyAccess("_GObjectArrayForDebugVisualizers");

var _GComplexObjectPathDebug = Module["_GComplexObjectPathDebug"] = makeInvalidEarlyAccess("_GComplexObjectPathDebug");

var _GObjectHandlePackageDebug = Module["_GObjectHandlePackageDebug"] = makeInvalidEarlyAccess("_GObjectHandlePackageDebug");

var _GDebuggingState = Module["_GDebuggingState"] = makeInvalidEarlyAccess("_GDebuggingState");

var _GFixedTagStoresForDebugVisualizers = Module["_GFixedTagStoresForDebugVisualizers"] = makeInvalidEarlyAccess("_GFixedTagStoresForDebugVisualizers");

var wasmTable = makeInvalidEarlyAccess("wasmTable");

function assignWasmExports(wasmExports) {
  assert(typeof wasmExports["free"] != "undefined", "missing Wasm export: free");
  assert(typeof wasmExports["malloc"] != "undefined", "missing Wasm export: malloc");
  assert(typeof wasmExports["fflush"] != "undefined", "missing Wasm export: fflush");
  assert(typeof wasmExports["pthread_self"] != "undefined", "missing Wasm export: pthread_self");
  assert(typeof wasmExports["wasmfs_create_memory_backend"] != "undefined", "missing Wasm export: wasmfs_create_memory_backend");
  assert(typeof wasmExports["wasmfs_create_file"] != "undefined", "missing Wasm export: wasmfs_create_file");
  assert(typeof wasmExports["__main_argc_argv"] != "undefined", "missing Wasm export: __main_argc_argv");
  assert(typeof wasmExports["PhoenixSessionResponse"] != "undefined", "missing Wasm export: PhoenixSessionResponse");
  assert(typeof wasmExports["PhoenixTransportReceive"] != "undefined", "missing Wasm export: PhoenixTransportReceive");
  assert(typeof wasmExports["PhoenixFleetStorage"] != "undefined", "missing Wasm export: PhoenixFleetStorage");
  assert(typeof wasmExports["PhoenixModDownloaded"] != "undefined", "missing Wasm export: PhoenixModDownloaded");
  assert(typeof wasmExports["PhoenixImportSnapshot"] != "undefined", "missing Wasm export: PhoenixImportSnapshot");
  assert(typeof wasmExports["PhoenixWebAction"] != "undefined", "missing Wasm export: PhoenixWebAction");
  assert(typeof wasmExports["PhoenixWebAuthoredAction"] != "undefined", "missing Wasm export: PhoenixWebAuthoredAction");
  assert(typeof wasmExports["PhoenixWebHostAction"] != "undefined", "missing Wasm export: PhoenixWebHostAction");
  assert(typeof wasmExports["emwgpuCreateBindGroup"] != "undefined", "missing Wasm export: emwgpuCreateBindGroup");
  assert(typeof wasmExports["emwgpuCreateBindGroupLayout"] != "undefined", "missing Wasm export: emwgpuCreateBindGroupLayout");
  assert(typeof wasmExports["emwgpuCreateCommandBuffer"] != "undefined", "missing Wasm export: emwgpuCreateCommandBuffer");
  assert(typeof wasmExports["emwgpuCreateCommandEncoder"] != "undefined", "missing Wasm export: emwgpuCreateCommandEncoder");
  assert(typeof wasmExports["emwgpuCreateComputePassEncoder"] != "undefined", "missing Wasm export: emwgpuCreateComputePassEncoder");
  assert(typeof wasmExports["emwgpuCreateComputePipeline"] != "undefined", "missing Wasm export: emwgpuCreateComputePipeline");
  assert(typeof wasmExports["emwgpuCreateExternalTexture"] != "undefined", "missing Wasm export: emwgpuCreateExternalTexture");
  assert(typeof wasmExports["emwgpuCreatePipelineLayout"] != "undefined", "missing Wasm export: emwgpuCreatePipelineLayout");
  assert(typeof wasmExports["emwgpuCreateQuerySet"] != "undefined", "missing Wasm export: emwgpuCreateQuerySet");
  assert(typeof wasmExports["emwgpuCreateRenderBundle"] != "undefined", "missing Wasm export: emwgpuCreateRenderBundle");
  assert(typeof wasmExports["emwgpuCreateRenderBundleEncoder"] != "undefined", "missing Wasm export: emwgpuCreateRenderBundleEncoder");
  assert(typeof wasmExports["emwgpuCreateRenderPassEncoder"] != "undefined", "missing Wasm export: emwgpuCreateRenderPassEncoder");
  assert(typeof wasmExports["emwgpuCreateRenderPipeline"] != "undefined", "missing Wasm export: emwgpuCreateRenderPipeline");
  assert(typeof wasmExports["emwgpuCreateSampler"] != "undefined", "missing Wasm export: emwgpuCreateSampler");
  assert(typeof wasmExports["emwgpuCreateSurface"] != "undefined", "missing Wasm export: emwgpuCreateSurface");
  assert(typeof wasmExports["emwgpuCreateTexture"] != "undefined", "missing Wasm export: emwgpuCreateTexture");
  assert(typeof wasmExports["emwgpuCreateTextureView"] != "undefined", "missing Wasm export: emwgpuCreateTextureView");
  assert(typeof wasmExports["emwgpuCreateAdapter"] != "undefined", "missing Wasm export: emwgpuCreateAdapter");
  assert(typeof wasmExports["emwgpuImportBuffer"] != "undefined", "missing Wasm export: emwgpuImportBuffer");
  assert(typeof wasmExports["emwgpuCreateDevice"] != "undefined", "missing Wasm export: emwgpuCreateDevice");
  assert(typeof wasmExports["emwgpuCreateQueue"] != "undefined", "missing Wasm export: emwgpuCreateQueue");
  assert(typeof wasmExports["emwgpuCreateShaderModule"] != "undefined", "missing Wasm export: emwgpuCreateShaderModule");
  assert(typeof wasmExports["emwgpuOnCompilationInfoCompleted"] != "undefined", "missing Wasm export: emwgpuOnCompilationInfoCompleted");
  assert(typeof wasmExports["emwgpuOnCreateComputePipelineCompleted"] != "undefined", "missing Wasm export: emwgpuOnCreateComputePipelineCompleted");
  assert(typeof wasmExports["emwgpuOnCreateRenderPipelineCompleted"] != "undefined", "missing Wasm export: emwgpuOnCreateRenderPipelineCompleted");
  assert(typeof wasmExports["emwgpuOnDeviceLostCompleted"] != "undefined", "missing Wasm export: emwgpuOnDeviceLostCompleted");
  assert(typeof wasmExports["emwgpuOnMapAsyncCompleted"] != "undefined", "missing Wasm export: emwgpuOnMapAsyncCompleted");
  assert(typeof wasmExports["emwgpuOnPopErrorScopeCompleted"] != "undefined", "missing Wasm export: emwgpuOnPopErrorScopeCompleted");
  assert(typeof wasmExports["emwgpuOnRequestAdapterCompleted"] != "undefined", "missing Wasm export: emwgpuOnRequestAdapterCompleted");
  assert(typeof wasmExports["emwgpuOnRequestDeviceCompleted"] != "undefined", "missing Wasm export: emwgpuOnRequestDeviceCompleted");
  assert(typeof wasmExports["emwgpuOnWorkDoneCompleted"] != "undefined", "missing Wasm export: emwgpuOnWorkDoneCompleted");
  assert(typeof wasmExports["emwgpuOnUncapturedError"] != "undefined", "missing Wasm export: emwgpuOnUncapturedError");
  assert(typeof wasmExports["wgpuDeviceAddRef"] != "undefined", "missing Wasm export: wgpuDeviceAddRef");
  assert(typeof wasmExports["_emscripten_tls_init"] != "undefined", "missing Wasm export: _emscripten_tls_init");
  assert(typeof wasmExports["emscripten_builtin_memalign"] != "undefined", "missing Wasm export: emscripten_builtin_memalign");
  assert(typeof wasmExports["_emscripten_proxy_main"] != "undefined", "missing Wasm export: _emscripten_proxy_main");
  assert(typeof wasmExports["emscripten_stack_get_base"] != "undefined", "missing Wasm export: emscripten_stack_get_base");
  assert(typeof wasmExports["emscripten_stack_get_end"] != "undefined", "missing Wasm export: emscripten_stack_get_end");
  assert(typeof wasmExports["_emscripten_run_callback_on_thread"] != "undefined", "missing Wasm export: _emscripten_run_callback_on_thread");
  assert(typeof wasmExports["__funcs_on_exit"] != "undefined", "missing Wasm export: __funcs_on_exit");
  assert(typeof wasmExports["_emscripten_thread_init"] != "undefined", "missing Wasm export: _emscripten_thread_init");
  assert(typeof wasmExports["__set_thread_state"] != "undefined", "missing Wasm export: __set_thread_state");
  assert(typeof wasmExports["_emscripten_thread_crashed"] != "undefined", "missing Wasm export: _emscripten_thread_crashed");
  assert(typeof wasmExports["emscripten_proxy_execute_queue"] != "undefined", "missing Wasm export: emscripten_proxy_execute_queue");
  assert(typeof wasmExports["emscripten_proxy_finish"] != "undefined", "missing Wasm export: emscripten_proxy_finish");
  assert(typeof wasmExports["_emscripten_run_js_on_main_thread_done"] != "undefined", "missing Wasm export: _emscripten_run_js_on_main_thread_done");
  assert(typeof wasmExports["_emscripten_run_js_on_main_thread"] != "undefined", "missing Wasm export: _emscripten_run_js_on_main_thread");
  assert(typeof wasmExports["_emscripten_thread_free_data"] != "undefined", "missing Wasm export: _emscripten_thread_free_data");
  assert(typeof wasmExports["_emscripten_thread_exit"] != "undefined", "missing Wasm export: _emscripten_thread_exit");
  assert(typeof wasmExports["_emscripten_check_mailbox"] != "undefined", "missing Wasm export: _emscripten_check_mailbox");
  assert(typeof wasmExports["memalign"] != "undefined", "missing Wasm export: memalign");
  assert(typeof wasmExports["setThrew"] != "undefined", "missing Wasm export: setThrew");
  assert(typeof wasmExports["_emscripten_tempret_set"] != "undefined", "missing Wasm export: _emscripten_tempret_set");
  assert(typeof wasmExports["emscripten_stack_init"] != "undefined", "missing Wasm export: emscripten_stack_init");
  assert(typeof wasmExports["emscripten_stack_set_limits"] != "undefined", "missing Wasm export: emscripten_stack_set_limits");
  assert(typeof wasmExports["emscripten_stack_get_free"] != "undefined", "missing Wasm export: emscripten_stack_get_free");
  assert(typeof wasmExports["_emscripten_stack_restore"] != "undefined", "missing Wasm export: _emscripten_stack_restore");
  assert(typeof wasmExports["_emscripten_stack_alloc"] != "undefined", "missing Wasm export: _emscripten_stack_alloc");
  assert(typeof wasmExports["emscripten_stack_get_current"] != "undefined", "missing Wasm export: emscripten_stack_get_current");
  assert(typeof wasmExports["__cxa_decrement_exception_refcount"] != "undefined", "missing Wasm export: __cxa_decrement_exception_refcount");
  assert(typeof wasmExports["__cxa_increment_exception_refcount"] != "undefined", "missing Wasm export: __cxa_increment_exception_refcount");
  assert(typeof wasmExports["__get_exception_message"] != "undefined", "missing Wasm export: __get_exception_message");
  assert(typeof wasmExports["__cxa_can_catch"] != "undefined", "missing Wasm export: __cxa_can_catch");
  assert(typeof wasmExports["__cxa_get_exception_ptr"] != "undefined", "missing Wasm export: __cxa_get_exception_ptr");
  assert(typeof wasmExports["_wasmfs_fetch_get_file_url"] != "undefined", "missing Wasm export: _wasmfs_fetch_get_file_url");
  assert(typeof wasmExports["_wasmfs_fetch_get_chunk_size"] != "undefined", "missing Wasm export: _wasmfs_fetch_get_chunk_size");
  assert(typeof wasmExports["_wasmfs_read_file"] != "undefined", "missing Wasm export: _wasmfs_read_file");
  assert(typeof wasmExports["_wasmfs_write_file"] != "undefined", "missing Wasm export: _wasmfs_write_file");
  assert(typeof wasmExports["_wasmfs_mkdir"] != "undefined", "missing Wasm export: _wasmfs_mkdir");
  assert(typeof wasmExports["_wasmfs_rmdir"] != "undefined", "missing Wasm export: _wasmfs_rmdir");
  assert(typeof wasmExports["_wasmfs_open"] != "undefined", "missing Wasm export: _wasmfs_open");
  assert(typeof wasmExports["_wasmfs_mknod"] != "undefined", "missing Wasm export: _wasmfs_mknod");
  assert(typeof wasmExports["_wasmfs_unlink"] != "undefined", "missing Wasm export: _wasmfs_unlink");
  assert(typeof wasmExports["_wasmfs_chdir"] != "undefined", "missing Wasm export: _wasmfs_chdir");
  assert(typeof wasmExports["_wasmfs_symlink"] != "undefined", "missing Wasm export: _wasmfs_symlink");
  assert(typeof wasmExports["_wasmfs_readlink"] != "undefined", "missing Wasm export: _wasmfs_readlink");
  assert(typeof wasmExports["_wasmfs_write"] != "undefined", "missing Wasm export: _wasmfs_write");
  assert(typeof wasmExports["_wasmfs_pwrite"] != "undefined", "missing Wasm export: _wasmfs_pwrite");
  assert(typeof wasmExports["_wasmfs_chmod"] != "undefined", "missing Wasm export: _wasmfs_chmod");
  assert(typeof wasmExports["_wasmfs_fchmod"] != "undefined", "missing Wasm export: _wasmfs_fchmod");
  assert(typeof wasmExports["_wasmfs_lchmod"] != "undefined", "missing Wasm export: _wasmfs_lchmod");
  assert(typeof wasmExports["_wasmfs_llseek"] != "undefined", "missing Wasm export: _wasmfs_llseek");
  assert(typeof wasmExports["_wasmfs_rename"] != "undefined", "missing Wasm export: _wasmfs_rename");
  assert(typeof wasmExports["_wasmfs_read"] != "undefined", "missing Wasm export: _wasmfs_read");
  assert(typeof wasmExports["_wasmfs_pread"] != "undefined", "missing Wasm export: _wasmfs_pread");
  assert(typeof wasmExports["_wasmfs_truncate"] != "undefined", "missing Wasm export: _wasmfs_truncate");
  assert(typeof wasmExports["_wasmfs_ftruncate"] != "undefined", "missing Wasm export: _wasmfs_ftruncate");
  assert(typeof wasmExports["_wasmfs_close"] != "undefined", "missing Wasm export: _wasmfs_close");
  assert(typeof wasmExports["_wasmfs_mmap"] != "undefined", "missing Wasm export: _wasmfs_mmap");
  assert(typeof wasmExports["_wasmfs_msync"] != "undefined", "missing Wasm export: _wasmfs_msync");
  assert(typeof wasmExports["_wasmfs_munmap"] != "undefined", "missing Wasm export: _wasmfs_munmap");
  assert(typeof wasmExports["_wasmfs_utime"] != "undefined", "missing Wasm export: _wasmfs_utime");
  assert(typeof wasmExports["_wasmfs_stat"] != "undefined", "missing Wasm export: _wasmfs_stat");
  assert(typeof wasmExports["_wasmfs_lstat"] != "undefined", "missing Wasm export: _wasmfs_lstat");
  assert(typeof wasmExports["_wasmfs_mount"] != "undefined", "missing Wasm export: _wasmfs_mount");
  assert(typeof wasmExports["_wasmfs_identify"] != "undefined", "missing Wasm export: _wasmfs_identify");
  assert(typeof wasmExports["_wasmfs_readdir_start"] != "undefined", "missing Wasm export: _wasmfs_readdir_start");
  assert(typeof wasmExports["_wasmfs_readdir_get"] != "undefined", "missing Wasm export: _wasmfs_readdir_get");
  assert(typeof wasmExports["_wasmfs_readdir_finish"] != "undefined", "missing Wasm export: _wasmfs_readdir_finish");
  assert(typeof wasmExports["_wasmfs_get_cwd"] != "undefined", "missing Wasm export: _wasmfs_get_cwd");
  assert(typeof wasmExports["wasmfs_create_jsimpl_backend"] != "undefined", "missing Wasm export: wasmfs_create_jsimpl_backend");
  assert(typeof wasmExports["_wasmfs_opfs_record_entry"] != "undefined", "missing Wasm export: _wasmfs_opfs_record_entry");
  assert(typeof wasmExports["wasmfs_unmount"] != "undefined", "missing Wasm export: wasmfs_unmount");
  assert(typeof wasmExports["wasmfs_flush"] != "undefined", "missing Wasm export: wasmfs_flush");
  assert(typeof wasmExports["__indirect_function_table"] != "undefined", "missing Wasm export: __indirect_function_table");
  assert(typeof wasmExports["GNameBlocksDebug"] != "undefined", "missing Wasm export: GNameBlocksDebug");
  assert(typeof wasmExports["GObjectArrayForDebugVisualizers"] != "undefined", "missing Wasm export: GObjectArrayForDebugVisualizers");
  assert(typeof wasmExports["GComplexObjectPathDebug"] != "undefined", "missing Wasm export: GComplexObjectPathDebug");
  assert(typeof wasmExports["GObjectHandlePackageDebug"] != "undefined", "missing Wasm export: GObjectHandlePackageDebug");
  assert(typeof wasmExports["GDebuggingState"] != "undefined", "missing Wasm export: GDebuggingState");
  assert(typeof wasmExports["GFixedTagStoresForDebugVisualizers"] != "undefined", "missing Wasm export: GFixedTagStoresForDebugVisualizers");
  _free = createExportWrapper("free", wasmExports["free"], 1);
  _malloc = createExportWrapper("malloc", wasmExports["malloc"], 1);
  _fflush = createExportWrapper("fflush", wasmExports["fflush"], 1);
  _pthread_self = wasmExports["pthread_self"];
  _wasmfs_create_memory_backend = createExportWrapper("wasmfs_create_memory_backend", wasmExports["wasmfs_create_memory_backend"], 0);
  _wasmfs_create_file = createExportWrapper("wasmfs_create_file", wasmExports["wasmfs_create_file"], 3);
  _main = Module["_main"] = createExportWrapper("__main_argc_argv", wasmExports["__main_argc_argv"], 2);
  _PhoenixSessionResponse = Module["_PhoenixSessionResponse"] = createExportWrapper("PhoenixSessionResponse", wasmExports["PhoenixSessionResponse"], 3);
  _PhoenixTransportReceive = Module["_PhoenixTransportReceive"] = createExportWrapper("PhoenixTransportReceive", wasmExports["PhoenixTransportReceive"], 2);
  _PhoenixFleetStorage = Module["_PhoenixFleetStorage"] = createExportWrapper("PhoenixFleetStorage", wasmExports["PhoenixFleetStorage"], 4);
  _PhoenixModDownloaded = Module["_PhoenixModDownloaded"] = createExportWrapper("PhoenixModDownloaded", wasmExports["PhoenixModDownloaded"], 4);
  _PhoenixImportSnapshot = Module["_PhoenixImportSnapshot"] = createExportWrapper("PhoenixImportSnapshot", wasmExports["PhoenixImportSnapshot"], 1);
  _PhoenixWebAction = Module["_PhoenixWebAction"] = createExportWrapper("PhoenixWebAction", wasmExports["PhoenixWebAction"], 3);
  _PhoenixWebAuthoredAction = Module["_PhoenixWebAuthoredAction"] = createExportWrapper("PhoenixWebAuthoredAction", wasmExports["PhoenixWebAuthoredAction"], 5);
  _PhoenixWebHostAction = Module["_PhoenixWebHostAction"] = createExportWrapper("PhoenixWebHostAction", wasmExports["PhoenixWebHostAction"], 1);
  _emwgpuCreateBindGroup = createExportWrapper("emwgpuCreateBindGroup", wasmExports["emwgpuCreateBindGroup"], 1);
  _emwgpuCreateBindGroupLayout = createExportWrapper("emwgpuCreateBindGroupLayout", wasmExports["emwgpuCreateBindGroupLayout"], 1);
  _emwgpuCreateCommandBuffer = createExportWrapper("emwgpuCreateCommandBuffer", wasmExports["emwgpuCreateCommandBuffer"], 1);
  _emwgpuCreateCommandEncoder = createExportWrapper("emwgpuCreateCommandEncoder", wasmExports["emwgpuCreateCommandEncoder"], 1);
  _emwgpuCreateComputePassEncoder = createExportWrapper("emwgpuCreateComputePassEncoder", wasmExports["emwgpuCreateComputePassEncoder"], 1);
  _emwgpuCreateComputePipeline = createExportWrapper("emwgpuCreateComputePipeline", wasmExports["emwgpuCreateComputePipeline"], 1);
  _emwgpuCreateExternalTexture = createExportWrapper("emwgpuCreateExternalTexture", wasmExports["emwgpuCreateExternalTexture"], 1);
  _emwgpuCreatePipelineLayout = createExportWrapper("emwgpuCreatePipelineLayout", wasmExports["emwgpuCreatePipelineLayout"], 1);
  _emwgpuCreateQuerySet = createExportWrapper("emwgpuCreateQuerySet", wasmExports["emwgpuCreateQuerySet"], 1);
  _emwgpuCreateRenderBundle = createExportWrapper("emwgpuCreateRenderBundle", wasmExports["emwgpuCreateRenderBundle"], 1);
  _emwgpuCreateRenderBundleEncoder = createExportWrapper("emwgpuCreateRenderBundleEncoder", wasmExports["emwgpuCreateRenderBundleEncoder"], 1);
  _emwgpuCreateRenderPassEncoder = createExportWrapper("emwgpuCreateRenderPassEncoder", wasmExports["emwgpuCreateRenderPassEncoder"], 1);
  _emwgpuCreateRenderPipeline = createExportWrapper("emwgpuCreateRenderPipeline", wasmExports["emwgpuCreateRenderPipeline"], 1);
  _emwgpuCreateSampler = createExportWrapper("emwgpuCreateSampler", wasmExports["emwgpuCreateSampler"], 1);
  _emwgpuCreateSurface = createExportWrapper("emwgpuCreateSurface", wasmExports["emwgpuCreateSurface"], 1);
  _emwgpuCreateTexture = createExportWrapper("emwgpuCreateTexture", wasmExports["emwgpuCreateTexture"], 1);
  _emwgpuCreateTextureView = createExportWrapper("emwgpuCreateTextureView", wasmExports["emwgpuCreateTextureView"], 1);
  _emwgpuCreateAdapter = createExportWrapper("emwgpuCreateAdapter", wasmExports["emwgpuCreateAdapter"], 1);
  _emwgpuImportBuffer = createExportWrapper("emwgpuImportBuffer", wasmExports["emwgpuImportBuffer"], 1);
  _emwgpuCreateDevice = createExportWrapper("emwgpuCreateDevice", wasmExports["emwgpuCreateDevice"], 2);
  _emwgpuCreateQueue = createExportWrapper("emwgpuCreateQueue", wasmExports["emwgpuCreateQueue"], 1);
  _emwgpuCreateShaderModule = createExportWrapper("emwgpuCreateShaderModule", wasmExports["emwgpuCreateShaderModule"], 1);
  _emwgpuOnCompilationInfoCompleted = createExportWrapper("emwgpuOnCompilationInfoCompleted", wasmExports["emwgpuOnCompilationInfoCompleted"], 3);
  _emwgpuOnCreateComputePipelineCompleted = createExportWrapper("emwgpuOnCreateComputePipelineCompleted", wasmExports["emwgpuOnCreateComputePipelineCompleted"], 4);
  _emwgpuOnCreateRenderPipelineCompleted = createExportWrapper("emwgpuOnCreateRenderPipelineCompleted", wasmExports["emwgpuOnCreateRenderPipelineCompleted"], 4);
  _emwgpuOnDeviceLostCompleted = createExportWrapper("emwgpuOnDeviceLostCompleted", wasmExports["emwgpuOnDeviceLostCompleted"], 3);
  _emwgpuOnMapAsyncCompleted = createExportWrapper("emwgpuOnMapAsyncCompleted", wasmExports["emwgpuOnMapAsyncCompleted"], 3);
  _emwgpuOnPopErrorScopeCompleted = createExportWrapper("emwgpuOnPopErrorScopeCompleted", wasmExports["emwgpuOnPopErrorScopeCompleted"], 4);
  _emwgpuOnRequestAdapterCompleted = createExportWrapper("emwgpuOnRequestAdapterCompleted", wasmExports["emwgpuOnRequestAdapterCompleted"], 4);
  _emwgpuOnRequestDeviceCompleted = createExportWrapper("emwgpuOnRequestDeviceCompleted", wasmExports["emwgpuOnRequestDeviceCompleted"], 4);
  _emwgpuOnWorkDoneCompleted = createExportWrapper("emwgpuOnWorkDoneCompleted", wasmExports["emwgpuOnWorkDoneCompleted"], 2);
  _emwgpuOnUncapturedError = createExportWrapper("emwgpuOnUncapturedError", wasmExports["emwgpuOnUncapturedError"], 3);
  _wgpuDeviceAddRef = createExportWrapper("wgpuDeviceAddRef", wasmExports["wgpuDeviceAddRef"], 1);
  __emscripten_tls_init = createExportWrapper("_emscripten_tls_init", wasmExports["_emscripten_tls_init"], 0);
  _emscripten_builtin_memalign = createExportWrapper("emscripten_builtin_memalign", wasmExports["emscripten_builtin_memalign"], 2);
  __emscripten_proxy_main = Module["__emscripten_proxy_main"] = createExportWrapper("_emscripten_proxy_main", wasmExports["_emscripten_proxy_main"], 2);
  _emscripten_stack_get_base = wasmExports["emscripten_stack_get_base"];
  _emscripten_stack_get_end = wasmExports["emscripten_stack_get_end"];
  __emscripten_run_callback_on_thread = createExportWrapper("_emscripten_run_callback_on_thread", wasmExports["_emscripten_run_callback_on_thread"], 6);
  ___funcs_on_exit = createExportWrapper("__funcs_on_exit", wasmExports["__funcs_on_exit"], 0);
  __emscripten_thread_init = createExportWrapper("_emscripten_thread_init", wasmExports["_emscripten_thread_init"], 6);
  ___set_thread_state = createExportWrapper("__set_thread_state", wasmExports["__set_thread_state"], 4);
  __emscripten_thread_crashed = createExportWrapper("_emscripten_thread_crashed", wasmExports["_emscripten_thread_crashed"], 0);
  _emscripten_proxy_execute_queue = createExportWrapper("emscripten_proxy_execute_queue", wasmExports["emscripten_proxy_execute_queue"], 1);
  _emscripten_proxy_finish = createExportWrapper("emscripten_proxy_finish", wasmExports["emscripten_proxy_finish"], 1);
  __emscripten_run_js_on_main_thread_done = createExportWrapper("_emscripten_run_js_on_main_thread_done", wasmExports["_emscripten_run_js_on_main_thread_done"], 3);
  __emscripten_run_js_on_main_thread = createExportWrapper("_emscripten_run_js_on_main_thread", wasmExports["_emscripten_run_js_on_main_thread"], 5);
  __emscripten_thread_free_data = createExportWrapper("_emscripten_thread_free_data", wasmExports["_emscripten_thread_free_data"], 1);
  __emscripten_thread_exit = createExportWrapper("_emscripten_thread_exit", wasmExports["_emscripten_thread_exit"], 1);
  __emscripten_check_mailbox = createExportWrapper("_emscripten_check_mailbox", wasmExports["_emscripten_check_mailbox"], 0);
  _memalign = createExportWrapper("memalign", wasmExports["memalign"], 2);
  _setThrew = createExportWrapper("setThrew", wasmExports["setThrew"], 2);
  __emscripten_tempret_set = createExportWrapper("_emscripten_tempret_set", wasmExports["_emscripten_tempret_set"], 1);
  _emscripten_stack_init = wasmExports["emscripten_stack_init"];
  _emscripten_stack_set_limits = wasmExports["emscripten_stack_set_limits"];
  _emscripten_stack_get_free = wasmExports["emscripten_stack_get_free"];
  __emscripten_stack_restore = wasmExports["_emscripten_stack_restore"];
  __emscripten_stack_alloc = wasmExports["_emscripten_stack_alloc"];
  _emscripten_stack_get_current = wasmExports["emscripten_stack_get_current"];
  ___cxa_decrement_exception_refcount = createExportWrapper("__cxa_decrement_exception_refcount", wasmExports["__cxa_decrement_exception_refcount"], 1);
  ___cxa_increment_exception_refcount = createExportWrapper("__cxa_increment_exception_refcount", wasmExports["__cxa_increment_exception_refcount"], 1);
  ___get_exception_message = createExportWrapper("__get_exception_message", wasmExports["__get_exception_message"], 3);
  ___cxa_can_catch = createExportWrapper("__cxa_can_catch", wasmExports["__cxa_can_catch"], 3);
  ___cxa_get_exception_ptr = createExportWrapper("__cxa_get_exception_ptr", wasmExports["__cxa_get_exception_ptr"], 1);
  __wasmfs_fetch_get_file_url = createExportWrapper("_wasmfs_fetch_get_file_url", wasmExports["_wasmfs_fetch_get_file_url"], 1);
  __wasmfs_fetch_get_chunk_size = createExportWrapper("_wasmfs_fetch_get_chunk_size", wasmExports["_wasmfs_fetch_get_chunk_size"], 1);
  __wasmfs_read_file = createExportWrapper("_wasmfs_read_file", wasmExports["_wasmfs_read_file"], 3);
  __wasmfs_write_file = createExportWrapper("_wasmfs_write_file", wasmExports["_wasmfs_write_file"], 3);
  __wasmfs_mkdir = createExportWrapper("_wasmfs_mkdir", wasmExports["_wasmfs_mkdir"], 2);
  __wasmfs_rmdir = createExportWrapper("_wasmfs_rmdir", wasmExports["_wasmfs_rmdir"], 1);
  __wasmfs_open = createExportWrapper("_wasmfs_open", wasmExports["_wasmfs_open"], 3);
  __wasmfs_mknod = createExportWrapper("_wasmfs_mknod", wasmExports["_wasmfs_mknod"], 3);
  __wasmfs_unlink = createExportWrapper("_wasmfs_unlink", wasmExports["_wasmfs_unlink"], 1);
  __wasmfs_chdir = createExportWrapper("_wasmfs_chdir", wasmExports["_wasmfs_chdir"], 1);
  __wasmfs_symlink = createExportWrapper("_wasmfs_symlink", wasmExports["_wasmfs_symlink"], 2);
  __wasmfs_readlink = createExportWrapper("_wasmfs_readlink", wasmExports["_wasmfs_readlink"], 2);
  __wasmfs_write = createExportWrapper("_wasmfs_write", wasmExports["_wasmfs_write"], 3);
  __wasmfs_pwrite = createExportWrapper("_wasmfs_pwrite", wasmExports["_wasmfs_pwrite"], 4);
  __wasmfs_chmod = createExportWrapper("_wasmfs_chmod", wasmExports["_wasmfs_chmod"], 2);
  __wasmfs_fchmod = createExportWrapper("_wasmfs_fchmod", wasmExports["_wasmfs_fchmod"], 2);
  __wasmfs_lchmod = createExportWrapper("_wasmfs_lchmod", wasmExports["_wasmfs_lchmod"], 2);
  __wasmfs_llseek = createExportWrapper("_wasmfs_llseek", wasmExports["_wasmfs_llseek"], 3);
  __wasmfs_rename = createExportWrapper("_wasmfs_rename", wasmExports["_wasmfs_rename"], 2);
  __wasmfs_read = createExportWrapper("_wasmfs_read", wasmExports["_wasmfs_read"], 3);
  __wasmfs_pread = createExportWrapper("_wasmfs_pread", wasmExports["_wasmfs_pread"], 4);
  __wasmfs_truncate = createExportWrapper("_wasmfs_truncate", wasmExports["_wasmfs_truncate"], 2);
  __wasmfs_ftruncate = createExportWrapper("_wasmfs_ftruncate", wasmExports["_wasmfs_ftruncate"], 2);
  __wasmfs_close = createExportWrapper("_wasmfs_close", wasmExports["_wasmfs_close"], 1);
  __wasmfs_mmap = createExportWrapper("_wasmfs_mmap", wasmExports["_wasmfs_mmap"], 5);
  __wasmfs_msync = createExportWrapper("_wasmfs_msync", wasmExports["_wasmfs_msync"], 3);
  __wasmfs_munmap = createExportWrapper("_wasmfs_munmap", wasmExports["_wasmfs_munmap"], 2);
  __wasmfs_utime = createExportWrapper("_wasmfs_utime", wasmExports["_wasmfs_utime"], 3);
  __wasmfs_stat = createExportWrapper("_wasmfs_stat", wasmExports["_wasmfs_stat"], 2);
  __wasmfs_lstat = createExportWrapper("_wasmfs_lstat", wasmExports["_wasmfs_lstat"], 2);
  __wasmfs_mount = createExportWrapper("_wasmfs_mount", wasmExports["_wasmfs_mount"], 2);
  __wasmfs_identify = createExportWrapper("_wasmfs_identify", wasmExports["_wasmfs_identify"], 1);
  __wasmfs_readdir_start = createExportWrapper("_wasmfs_readdir_start", wasmExports["_wasmfs_readdir_start"], 1);
  __wasmfs_readdir_get = createExportWrapper("_wasmfs_readdir_get", wasmExports["_wasmfs_readdir_get"], 1);
  __wasmfs_readdir_finish = createExportWrapper("_wasmfs_readdir_finish", wasmExports["_wasmfs_readdir_finish"], 1);
  __wasmfs_get_cwd = createExportWrapper("_wasmfs_get_cwd", wasmExports["_wasmfs_get_cwd"], 0);
  _wasmfs_create_jsimpl_backend = createExportWrapper("wasmfs_create_jsimpl_backend", wasmExports["wasmfs_create_jsimpl_backend"], 0);
  __wasmfs_opfs_record_entry = createExportWrapper("_wasmfs_opfs_record_entry", wasmExports["_wasmfs_opfs_record_entry"], 3);
  _wasmfs_unmount = createExportWrapper("wasmfs_unmount", wasmExports["wasmfs_unmount"], 1);
  _wasmfs_flush = createExportWrapper("wasmfs_flush", wasmExports["wasmfs_flush"], 0);
  __indirect_function_table = wasmTable = wasmExports["__indirect_function_table"];
  _GNameBlocksDebug = Module["_GNameBlocksDebug"] = Number(wasmExports["GNameBlocksDebug"].value);
  _GObjectArrayForDebugVisualizers = Module["_GObjectArrayForDebugVisualizers"] = Number(wasmExports["GObjectArrayForDebugVisualizers"].value);
  _GComplexObjectPathDebug = Module["_GComplexObjectPathDebug"] = Number(wasmExports["GComplexObjectPathDebug"].value);
  _GObjectHandlePackageDebug = Module["_GObjectHandlePackageDebug"] = Number(wasmExports["GObjectHandlePackageDebug"].value);
  _GDebuggingState = Module["_GDebuggingState"] = Number(wasmExports["GDebuggingState"].value);
  _GFixedTagStoresForDebugVisualizers = Module["_GFixedTagStoresForDebugVisualizers"] = Number(wasmExports["GFixedTagStoresForDebugVisualizers"].value);
}

var wasmImports;

function assignWasmImports() {
  wasmImports = {
    /** @export */ AudioMixerWebAudio_Create,
    /** @export */ AudioMixerWebAudio_Destroy,
    /** @export */ AudioMixerWebAudio_HasNode,
    /** @export */ IasFetch_Abort,
    /** @export */ IasFetch_Copy,
    /** @export */ IasFetch_Start,
    /** @export */ WasmApplication_CanvasScale,
    /** @export */ WasmApplication_HasFinePointer,
    /** @export */ WasmApplication_HoldCanvasTouches,
    /** @export */ WasmApplication_ResizeCanvas,
    /** @export */ WasmApplication_SetCanvasCursor,
    /** @export */ WasmApplication_SetPointerCapture,
    /** @export */ WasmApplication_WatchLockLoss,
    /** @export */ WasmGamepad_Poll,
    /** @export */ WasmGamepad_Start,
    /** @export */ WasmIoWorker_Create,
    /** @export */ WasmIoWorker_PostFile,
    /** @export */ WasmIoWorker_PostRanges,
    /** @export */ WasmPage_InstallExec,
    /** @export */ WasmPlatformApplicationMisc_CanvasSize,
    /** @export */ WasmPlatformMisc_BroadcastLine,
    /** @export */ WasmPlatformMisc_TouchScreen,
    /** @export */ WasmRegex_Compile,
    /** @export */ WasmRegex_Find,
    /** @export */ WasmRegex_Install,
    /** @export */ WasmSockets_Close,
    /** @export */ WasmSockets_Drain,
    /** @export */ WasmSockets_Open,
    /** @export */ WasmStartup_ExitNode,
    /** @export */ WasmStartup_ExitWeb,
    /** @export */ WasmStartup_GetManifestAddress,
    /** @export */ WasmStartup_IsNode,
    /** @export */ WasmStartup_SavedMode,
    /** @export */ WasmTrace_Close,
    /** @export */ WasmTrace_Open,
    /** @export */ WasmTrace_Send,
    /** @export */ WebGPUAnswerPageDevice,
    /** @export */ WebGPUCallPageDeviceLost,
    /** @export */ WebGPUCallPagePipelineFailed,
    /** @export */ WebGPUHasPageDevice,
    /** @export */ WebGPUWatchPageDevice,
    /** @export */ __assert_fail: ___assert_fail,
    /** @export */ __call_sighandler: ___call_sighandler,
    /** @export */ __cxa_begin_catch: ___cxa_begin_catch,
    /** @export */ __cxa_end_catch: ___cxa_end_catch,
    /** @export */ __cxa_find_matching_catch_2: ___cxa_find_matching_catch_2,
    /** @export */ __cxa_find_matching_catch_3: ___cxa_find_matching_catch_3,
    /** @export */ __cxa_rethrow: ___cxa_rethrow,
    /** @export */ __cxa_throw: ___cxa_throw,
    /** @export */ __cxa_uncaught_exceptions: ___cxa_uncaught_exceptions,
    /** @export */ __pthread_create_js: ___pthread_create_js,
    /** @export */ __resumeException: ___resumeException,
    /** @export */ _abort_js: __abort_js,
    /** @export */ _emscripten_init_main_thread_js: __emscripten_init_main_thread_js,
    /** @export */ _emscripten_notify_mailbox_postmessage: __emscripten_notify_mailbox_postmessage,
    /** @export */ _emscripten_receive_on_main_thread_js: __emscripten_receive_on_main_thread_js,
    /** @export */ _emscripten_runtime_keepalive_clear: __emscripten_runtime_keepalive_clear,
    /** @export */ _emscripten_thread_cleanup: __emscripten_thread_cleanup,
    /** @export */ _emscripten_thread_mailbox_await: __emscripten_thread_mailbox_await,
    /** @export */ _emscripten_thread_set_strongref: __emscripten_thread_set_strongref,
    /** @export */ _emscripten_throw_longjmp: __emscripten_throw_longjmp,
    /** @export */ _gmtime_js: __gmtime_js,
    /** @export */ _localtime_js: __localtime_js,
    /** @export */ _tzset_js: __tzset_js,
    /** @export */ _wasmfs_copy_preloaded_file_data: __wasmfs_copy_preloaded_file_data,
    /** @export */ _wasmfs_create_fetch_backend_js: __wasmfs_create_fetch_backend_js,
    /** @export */ _wasmfs_get_num_preloaded_dirs: __wasmfs_get_num_preloaded_dirs,
    /** @export */ _wasmfs_get_num_preloaded_files: __wasmfs_get_num_preloaded_files,
    /** @export */ _wasmfs_get_preloaded_child_path: __wasmfs_get_preloaded_child_path,
    /** @export */ _wasmfs_get_preloaded_file_mode: __wasmfs_get_preloaded_file_mode,
    /** @export */ _wasmfs_get_preloaded_file_size: __wasmfs_get_preloaded_file_size,
    /** @export */ _wasmfs_get_preloaded_parent_path: __wasmfs_get_preloaded_parent_path,
    /** @export */ _wasmfs_get_preloaded_path_name: __wasmfs_get_preloaded_path_name,
    /** @export */ _wasmfs_jsimpl_alloc_file: __wasmfs_jsimpl_alloc_file,
    /** @export */ _wasmfs_jsimpl_async_alloc_file: __wasmfs_jsimpl_async_alloc_file,
    /** @export */ _wasmfs_jsimpl_async_free_file: __wasmfs_jsimpl_async_free_file,
    /** @export */ _wasmfs_jsimpl_async_get_size: __wasmfs_jsimpl_async_get_size,
    /** @export */ _wasmfs_jsimpl_async_read: __wasmfs_jsimpl_async_read,
    /** @export */ _wasmfs_jsimpl_async_write: __wasmfs_jsimpl_async_write,
    /** @export */ _wasmfs_jsimpl_free_file: __wasmfs_jsimpl_free_file,
    /** @export */ _wasmfs_jsimpl_get_size: __wasmfs_jsimpl_get_size,
    /** @export */ _wasmfs_jsimpl_read: __wasmfs_jsimpl_read,
    /** @export */ _wasmfs_jsimpl_set_size: __wasmfs_jsimpl_set_size,
    /** @export */ _wasmfs_jsimpl_write: __wasmfs_jsimpl_write,
    /** @export */ _wasmfs_opfs_close_access: __wasmfs_opfs_close_access,
    /** @export */ _wasmfs_opfs_close_blob: __wasmfs_opfs_close_blob,
    /** @export */ _wasmfs_opfs_flush_access: __wasmfs_opfs_flush_access,
    /** @export */ _wasmfs_opfs_free_directory: __wasmfs_opfs_free_directory,
    /** @export */ _wasmfs_opfs_free_file: __wasmfs_opfs_free_file,
    /** @export */ _wasmfs_opfs_get_child: __wasmfs_opfs_get_child,
    /** @export */ _wasmfs_opfs_get_entries: __wasmfs_opfs_get_entries,
    /** @export */ _wasmfs_opfs_get_size_access: __wasmfs_opfs_get_size_access,
    /** @export */ _wasmfs_opfs_get_size_blob: __wasmfs_opfs_get_size_blob,
    /** @export */ _wasmfs_opfs_get_size_file: __wasmfs_opfs_get_size_file,
    /** @export */ _wasmfs_opfs_init_root_directory: __wasmfs_opfs_init_root_directory,
    /** @export */ _wasmfs_opfs_insert_directory: __wasmfs_opfs_insert_directory,
    /** @export */ _wasmfs_opfs_insert_file: __wasmfs_opfs_insert_file,
    /** @export */ _wasmfs_opfs_move_file: __wasmfs_opfs_move_file,
    /** @export */ _wasmfs_opfs_open_access: __wasmfs_opfs_open_access,
    /** @export */ _wasmfs_opfs_open_blob: __wasmfs_opfs_open_blob,
    /** @export */ _wasmfs_opfs_read_access: __wasmfs_opfs_read_access,
    /** @export */ _wasmfs_opfs_read_blob: __wasmfs_opfs_read_blob,
    /** @export */ _wasmfs_opfs_remove_child: __wasmfs_opfs_remove_child,
    /** @export */ _wasmfs_opfs_set_size_access: __wasmfs_opfs_set_size_access,
    /** @export */ _wasmfs_opfs_set_size_file: __wasmfs_opfs_set_size_file,
    /** @export */ _wasmfs_opfs_write_access: __wasmfs_opfs_write_access,
    /** @export */ _wasmfs_stdin_get_char: __wasmfs_stdin_get_char,
    /** @export */ _wasmfs_thread_utils_heartbeat: __wasmfs_thread_utils_heartbeat,
    /** @export */ clock_time_get: _clock_time_get,
    /** @export */ emscripten_asm_const_async_on_main_thread: _emscripten_asm_const_async_on_main_thread,
    /** @export */ emscripten_asm_const_double: _emscripten_asm_const_double,
    /** @export */ emscripten_asm_const_double_sync_on_main_thread: _emscripten_asm_const_double_sync_on_main_thread,
    /** @export */ emscripten_asm_const_int: _emscripten_asm_const_int,
    /** @export */ emscripten_asm_const_int_sync_on_main_thread: _emscripten_asm_const_int_sync_on_main_thread,
    /** @export */ emscripten_check_blocking_allowed: _emscripten_check_blocking_allowed,
    /** @export */ emscripten_console_log: _emscripten_console_log,
    /** @export */ emscripten_date_now: _emscripten_date_now,
    /** @export */ emscripten_err: _emscripten_err,
    /** @export */ emscripten_exit_with_live_runtime: _emscripten_exit_with_live_runtime,
    /** @export */ emscripten_get_callstack: _emscripten_get_callstack,
    /** @export */ emscripten_get_heap_max: _emscripten_get_heap_max,
    /** @export */ emscripten_get_now: _emscripten_get_now,
    /** @export */ emscripten_get_visibility_status: _emscripten_get_visibility_status,
    /** @export */ emscripten_has_asyncify: _emscripten_has_asyncify,
    /** @export */ emscripten_num_logical_cores: _emscripten_num_logical_cores,
    /** @export */ emscripten_out: _emscripten_out,
    /** @export */ emscripten_request_animation_frame: _emscripten_request_animation_frame,
    /** @export */ emscripten_resize_heap: _emscripten_resize_heap,
    /** @export */ emscripten_return_address: _emscripten_return_address,
    /** @export */ emscripten_runtime_keepalive_check: _emscripten_runtime_keepalive_check,
    /** @export */ emscripten_set_blur_callback_on_thread: _emscripten_set_blur_callback_on_thread,
    /** @export */ emscripten_set_immediate: _emscripten_set_immediate,
    /** @export */ emscripten_set_keydown_callback_on_thread: _emscripten_set_keydown_callback_on_thread,
    /** @export */ emscripten_set_keyup_callback_on_thread: _emscripten_set_keyup_callback_on_thread,
    /** @export */ emscripten_set_mousedown_callback_on_thread: _emscripten_set_mousedown_callback_on_thread,
    /** @export */ emscripten_set_mouseenter_callback_on_thread: _emscripten_set_mouseenter_callback_on_thread,
    /** @export */ emscripten_set_mousemove_callback_on_thread: _emscripten_set_mousemove_callback_on_thread,
    /** @export */ emscripten_set_mouseup_callback_on_thread: _emscripten_set_mouseup_callback_on_thread,
    /** @export */ emscripten_set_pointerlockchange_callback_on_thread: _emscripten_set_pointerlockchange_callback_on_thread,
    /** @export */ emscripten_set_resize_callback_on_thread: _emscripten_set_resize_callback_on_thread,
    /** @export */ emscripten_set_timeout: _emscripten_set_timeout,
    /** @export */ emscripten_set_touchcancel_callback_on_thread: _emscripten_set_touchcancel_callback_on_thread,
    /** @export */ emscripten_set_touchend_callback_on_thread: _emscripten_set_touchend_callback_on_thread,
    /** @export */ emscripten_set_touchmove_callback_on_thread: _emscripten_set_touchmove_callback_on_thread,
    /** @export */ emscripten_set_touchstart_callback_on_thread: _emscripten_set_touchstart_callback_on_thread,
    /** @export */ emscripten_set_visibilitychange_callback_on_thread: _emscripten_set_visibilitychange_callback_on_thread,
    /** @export */ emscripten_set_wheel_callback_on_thread: _emscripten_set_wheel_callback_on_thread,
    /** @export */ emscripten_unwind_to_js_event_loop: _emscripten_unwind_to_js_event_loop,
    /** @export */ emscripten_webgpu_get_device: _emscripten_webgpu_get_device,
    /** @export */ emwgpuAdapterRequestDevice: _emwgpuAdapterRequestDevice,
    /** @export */ emwgpuBufferDestroy: _emwgpuBufferDestroy,
    /** @export */ emwgpuBufferGetConstMappedRange: _emwgpuBufferGetConstMappedRange,
    /** @export */ emwgpuBufferGetMappedRange: _emwgpuBufferGetMappedRange,
    /** @export */ emwgpuBufferMapAsync: _emwgpuBufferMapAsync,
    /** @export */ emwgpuBufferUnmap: _emwgpuBufferUnmap,
    /** @export */ emwgpuDelete: _emwgpuDelete,
    /** @export */ emwgpuDeviceCreateBuffer: _emwgpuDeviceCreateBuffer,
    /** @export */ emwgpuDeviceCreateComputePipelineAsync: _emwgpuDeviceCreateComputePipelineAsync,
    /** @export */ emwgpuDeviceCreateRenderPipelineAsync: _emwgpuDeviceCreateRenderPipelineAsync,
    /** @export */ emwgpuDeviceCreateShaderModule: _emwgpuDeviceCreateShaderModule,
    /** @export */ emwgpuDeviceDestroy: _emwgpuDeviceDestroy,
    /** @export */ emwgpuDevicePopErrorScope: _emwgpuDevicePopErrorScope,
    /** @export */ emwgpuGetPreferredFormat: _emwgpuGetPreferredFormat,
    /** @export */ emwgpuInstanceRequestAdapter: _emwgpuInstanceRequestAdapter,
    /** @export */ emwgpuQueueOnSubmittedWorkDone: _emwgpuQueueOnSubmittedWorkDone,
    /** @export */ environ_get: _environ_get,
    /** @export */ environ_sizes_get: _environ_sizes_get,
    /** @export */ exit: _exit,
    /** @export */ invoke_djjj,
    /** @export */ invoke_fjjj,
    /** @export */ invoke_iij,
    /** @export */ invoke_iijj,
    /** @export */ invoke_iijjjjiijjjjj,
    /** @export */ invoke_ij,
    /** @export */ invoke_iji,
    /** @export */ invoke_ijj,
    /** @export */ invoke_ijji,
    /** @export */ invoke_ijjijijjjjjj,
    /** @export */ invoke_ijjijj,
    /** @export */ invoke_ijjj,
    /** @export */ invoke_ijjjj,
    /** @export */ invoke_j,
    /** @export */ invoke_jiijjjj,
    /** @export */ invoke_jijj,
    /** @export */ invoke_jj,
    /** @export */ invoke_jji,
    /** @export */ invoke_jjij,
    /** @export */ invoke_jjj,
    /** @export */ invoke_jjji,
    /** @export */ invoke_jjjj,
    /** @export */ invoke_jjjjij,
    /** @export */ invoke_jjjjj,
    /** @export */ invoke_jjjjjj,
    /** @export */ invoke_jjjjjji,
    /** @export */ invoke_jjjjjjji,
    /** @export */ invoke_jjjjjjjj,
    /** @export */ invoke_v,
    /** @export */ invoke_vi,
    /** @export */ invoke_viiijij,
    /** @export */ invoke_viijjjj,
    /** @export */ invoke_viijjjjjjjj,
    /** @export */ invoke_vij,
    /** @export */ invoke_vijjjjjjjjj,
    /** @export */ invoke_vj,
    /** @export */ invoke_vji,
    /** @export */ invoke_vjii,
    /** @export */ invoke_vjiii,
    /** @export */ invoke_vjj,
    /** @export */ invoke_vjji,
    /** @export */ invoke_vjjiiiiiii,
    /** @export */ invoke_vjjij,
    /** @export */ invoke_vjjj,
    /** @export */ invoke_vjjji,
    /** @export */ invoke_vjjjijjjijiijjji,
    /** @export */ invoke_vjjjj,
    /** @export */ invoke_vjjjjjjj,
    /** @export */ memory: wasmMemory,
    /** @export */ proc_exit: _proc_exit,
    /** @export */ random_get: _random_get,
    /** @export */ wgpuAdapterGetFeatures: _wgpuAdapterGetFeatures,
    /** @export */ wgpuAdapterGetInfo: _wgpuAdapterGetInfo,
    /** @export */ wgpuAdapterGetLimits: _wgpuAdapterGetLimits,
    /** @export */ wgpuAdapterHasFeature: _wgpuAdapterHasFeature,
    /** @export */ wgpuCommandEncoderBeginComputePass: _wgpuCommandEncoderBeginComputePass,
    /** @export */ wgpuCommandEncoderBeginRenderPass: _wgpuCommandEncoderBeginRenderPass,
    /** @export */ wgpuCommandEncoderClearBuffer: _wgpuCommandEncoderClearBuffer,
    /** @export */ wgpuCommandEncoderCopyBufferToBuffer: _wgpuCommandEncoderCopyBufferToBuffer,
    /** @export */ wgpuCommandEncoderCopyBufferToTexture: _wgpuCommandEncoderCopyBufferToTexture,
    /** @export */ wgpuCommandEncoderCopyTextureToBuffer: _wgpuCommandEncoderCopyTextureToBuffer,
    /** @export */ wgpuCommandEncoderCopyTextureToTexture: _wgpuCommandEncoderCopyTextureToTexture,
    /** @export */ wgpuCommandEncoderFinish: _wgpuCommandEncoderFinish,
    /** @export */ wgpuCommandEncoderResolveQuerySet: _wgpuCommandEncoderResolveQuerySet,
    /** @export */ wgpuComputePassEncoderDispatchWorkgroups: _wgpuComputePassEncoderDispatchWorkgroups,
    /** @export */ wgpuComputePassEncoderDispatchWorkgroupsIndirect: _wgpuComputePassEncoderDispatchWorkgroupsIndirect,
    /** @export */ wgpuComputePassEncoderEnd: _wgpuComputePassEncoderEnd,
    /** @export */ wgpuComputePassEncoderSetBindGroup: _wgpuComputePassEncoderSetBindGroup,
    /** @export */ wgpuComputePassEncoderSetPipeline: _wgpuComputePassEncoderSetPipeline,
    /** @export */ wgpuComputePipelineGetBindGroupLayout: _wgpuComputePipelineGetBindGroupLayout,
    /** @export */ wgpuDeviceCreateBindGroup: _wgpuDeviceCreateBindGroup,
    /** @export */ wgpuDeviceCreateBindGroupLayout: _wgpuDeviceCreateBindGroupLayout,
    /** @export */ wgpuDeviceCreateCommandEncoder: _wgpuDeviceCreateCommandEncoder,
    /** @export */ wgpuDeviceCreateComputePipeline: _wgpuDeviceCreateComputePipeline,
    /** @export */ wgpuDeviceCreatePipelineLayout: _wgpuDeviceCreatePipelineLayout,
    /** @export */ wgpuDeviceCreateQuerySet: _wgpuDeviceCreateQuerySet,
    /** @export */ wgpuDeviceCreateRenderBundleEncoder: _wgpuDeviceCreateRenderBundleEncoder,
    /** @export */ wgpuDeviceCreateRenderPipeline: _wgpuDeviceCreateRenderPipeline,
    /** @export */ wgpuDeviceCreateSampler: _wgpuDeviceCreateSampler,
    /** @export */ wgpuDeviceCreateTexture: _wgpuDeviceCreateTexture,
    /** @export */ wgpuDeviceGetFeatures: _wgpuDeviceGetFeatures,
    /** @export */ wgpuDeviceGetLimits: _wgpuDeviceGetLimits,
    /** @export */ wgpuDeviceHasFeature: _wgpuDeviceHasFeature,
    /** @export */ wgpuDevicePushErrorScope: _wgpuDevicePushErrorScope,
    /** @export */ wgpuInstanceCreateSurface: _wgpuInstanceCreateSurface,
    /** @export */ wgpuQueueSubmit: _wgpuQueueSubmit,
    /** @export */ wgpuQueueWriteBuffer: _wgpuQueueWriteBuffer,
    /** @export */ wgpuQueueWriteTexture: _wgpuQueueWriteTexture,
    /** @export */ wgpuRenderBundleEncoderDraw: _wgpuRenderBundleEncoderDraw,
    /** @export */ wgpuRenderBundleEncoderDrawIndexed: _wgpuRenderBundleEncoderDrawIndexed,
    /** @export */ wgpuRenderBundleEncoderFinish: _wgpuRenderBundleEncoderFinish,
    /** @export */ wgpuRenderBundleEncoderSetBindGroup: _wgpuRenderBundleEncoderSetBindGroup,
    /** @export */ wgpuRenderBundleEncoderSetIndexBuffer: _wgpuRenderBundleEncoderSetIndexBuffer,
    /** @export */ wgpuRenderBundleEncoderSetPipeline: _wgpuRenderBundleEncoderSetPipeline,
    /** @export */ wgpuRenderBundleEncoderSetVertexBuffer: _wgpuRenderBundleEncoderSetVertexBuffer,
    /** @export */ wgpuRenderPassEncoderBeginOcclusionQuery: _wgpuRenderPassEncoderBeginOcclusionQuery,
    /** @export */ wgpuRenderPassEncoderDraw: _wgpuRenderPassEncoderDraw,
    /** @export */ wgpuRenderPassEncoderDrawIndexed: _wgpuRenderPassEncoderDrawIndexed,
    /** @export */ wgpuRenderPassEncoderDrawIndexedIndirect: _wgpuRenderPassEncoderDrawIndexedIndirect,
    /** @export */ wgpuRenderPassEncoderDrawIndirect: _wgpuRenderPassEncoderDrawIndirect,
    /** @export */ wgpuRenderPassEncoderEnd: _wgpuRenderPassEncoderEnd,
    /** @export */ wgpuRenderPassEncoderEndOcclusionQuery: _wgpuRenderPassEncoderEndOcclusionQuery,
    /** @export */ wgpuRenderPassEncoderExecuteBundles: _wgpuRenderPassEncoderExecuteBundles,
    /** @export */ wgpuRenderPassEncoderSetBindGroup: _wgpuRenderPassEncoderSetBindGroup,
    /** @export */ wgpuRenderPassEncoderSetBlendConstant: _wgpuRenderPassEncoderSetBlendConstant,
    /** @export */ wgpuRenderPassEncoderSetIndexBuffer: _wgpuRenderPassEncoderSetIndexBuffer,
    /** @export */ wgpuRenderPassEncoderSetPipeline: _wgpuRenderPassEncoderSetPipeline,
    /** @export */ wgpuRenderPassEncoderSetScissorRect: _wgpuRenderPassEncoderSetScissorRect,
    /** @export */ wgpuRenderPassEncoderSetStencilReference: _wgpuRenderPassEncoderSetStencilReference,
    /** @export */ wgpuRenderPassEncoderSetVertexBuffer: _wgpuRenderPassEncoderSetVertexBuffer,
    /** @export */ wgpuRenderPassEncoderSetViewport: _wgpuRenderPassEncoderSetViewport,
    /** @export */ wgpuSurfaceConfigure: _wgpuSurfaceConfigure,
    /** @export */ wgpuSurfaceGetCurrentTexture: _wgpuSurfaceGetCurrentTexture,
    /** @export */ wgpuSurfacePresent: _wgpuSurfacePresent,
    /** @export */ wgpuTextureCreateView: _wgpuTextureCreateView,
    /** @export */ wgpuTextureDestroy: _wgpuTextureDestroy,
    /** @export */ wgpuTextureGetHeight: _wgpuTextureGetHeight,
    /** @export */ wgpuTextureGetWidth: _wgpuTextureGetWidth
  };
}

function invoke_vj(index, a1) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_vjji(index, a1, a2, a3) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1, a2, a3);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_ij(index, a1) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_vji(index, a1, a2) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1, a2);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_vjjji(index, a1, a2, a3, a4) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1, a2, a3, a4);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_vi(index, a1) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_ijjijj(index, a1, a2, a3, a4, a5) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3, a4, a5);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_jjjjjjjj(index, a1, a2, a3, a4, a5, a6, a7) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3, a4, a5, a6, a7);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
    return 0n;
  }
}

function invoke_jj(index, a1) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
    return 0n;
  }
}

function invoke_jjj(index, a1, a2) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
    return 0n;
  }
}

function invoke_jjjj(index, a1, a2, a3) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
    return 0n;
  }
}

function invoke_v(index) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))();
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_viijjjj(index, a1, a2, a3, a4, a5, a6) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1, a2, a3, a4, a5, a6);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_vjjj(index, a1, a2, a3) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1, a2, a3);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_viiijij(index, a1, a2, a3, a4, a5, a6) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1, a2, a3, a4, a5, a6);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_jjji(index, a1, a2, a3) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
    return 0n;
  }
}

function invoke_vjjiiiiiii(index, a1, a2, a3, a4, a5, a6, a7, a8, a9) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1, a2, a3, a4, a5, a6, a7, a8, a9);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_vjjjj(index, a1, a2, a3, a4) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1, a2, a3, a4);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_iijj(index, a1, a2, a3) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_vjii(index, a1, a2, a3) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1, a2, a3);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_vjjij(index, a1, a2, a3, a4) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1, a2, a3, a4);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_vjj(index, a1, a2) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1, a2);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_ijjj(index, a1, a2, a3) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_ijjjj(index, a1, a2, a3, a4) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3, a4);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_jiijjjj(index, a1, a2, a3, a4, a5, a6) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3, a4, a5, a6);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
    return 0n;
  }
}

function invoke_vjiii(index, a1, a2, a3, a4) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1, a2, a3, a4);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_ijj(index, a1, a2) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_iij(index, a1, a2) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_vij(index, a1, a2) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1, a2);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_iji(index, a1, a2) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_jjjjij(index, a1, a2, a3, a4, a5) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3, a4, a5);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
    return 0n;
  }
}

function invoke_jji(index, a1, a2) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
    return 0n;
  }
}

function invoke_jjjjjjji(index, a1, a2, a3, a4, a5, a6, a7) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3, a4, a5, a6, a7);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
    return 0n;
  }
}

function invoke_iijjjjiijjjjj(index, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_fjjj(index, a1, a2, a3) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_djjj(index, a1, a2, a3) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_jjjjj(index, a1, a2, a3, a4) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3, a4);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
    return 0n;
  }
}

function invoke_vjjjjjjj(index, a1, a2, a3, a4, a5, a6, a7) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1, a2, a3, a4, a5, a6, a7);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_j(index) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))();
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
    return 0n;
  }
}

function invoke_jjjjjji(index, a1, a2, a3, a4, a5, a6) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3, a4, a5, a6);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
    return 0n;
  }
}

function invoke_ijjijijjjjjj(index, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_vijjjjjjjjj(index, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_ijji(index, a1, a2, a3) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_viijjjjjjjj(index, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_vjjjijjjijiijjji(index, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) {
  var sp = stackSave();
  try {
    getWasmTableEntry(Number(index))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
  }
}

function invoke_jijj(index, a1, a2, a3) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
    return 0n;
  }
}

function invoke_jjjjjj(index, a1, a2, a3, a4, a5) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3, a4, a5);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
    return 0n;
  }
}

function invoke_jjij(index, a1, a2, a3) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(Number(index))(a1, a2, a3);
  } catch (e) {
    stackRestore(sp);
    if (!(e instanceof EmscriptenEH)) throw e;
    _setThrew(1, 0);
    return 0n;
  }
}

// Argument name here must shadow the `wasmExports` global so
// that it is recognised by metadce and minify-import-export-names
// passes.
function applySignatureConversions(wasmExports) {
  // First, make a copy of the incoming exports object
  wasmExports = Object.assign({}, wasmExports);
  var makeWrapper__p = f => a0 => f(BigInt(a0));
  var makeWrapper_pp = f => a0 => Number(f(BigInt(a0)));
  var makeWrapper_p = f => () => Number(f());
  var makeWrapper__p_p = f => (a0, a1, a2) => f(BigInt(a0), a1, BigInt(a2));
  var makeWrapper___PP = f => (a0, a1, a2) => f(a0, BigInt(a1 ? a1 : 0), BigInt(a2 ? a2 : 0));
  var makeWrapper_ppp = f => (a0, a1) => Number(f(BigInt(a0), BigInt(a1)));
  var makeWrapper__pp_ppp = f => (a0, a1, a2, a3, a4, a5) => f(BigInt(a0), BigInt(a1), a2, BigInt(a3), BigInt(a4), BigInt(a5));
  var makeWrapper__p_____ = f => (a0, a1, a2, a3, a4, a5) => f(BigInt(a0), a1, a2, a3, a4, a5);
  var makeWrapper__p___ = f => (a0, a1, a2, a3) => f(BigInt(a0), a1, a2, a3);
  var makeWrapper__pp_ = f => (a0, a1, a2) => f(BigInt(a0), BigInt(a1), a2);
  var makeWrapper___p_p_ = f => (a0, a1, a2, a3, a4) => f(a0, BigInt(a1), a2, BigInt(a3), a4);
  var makeWrapper__p_ = f => (a0, a1) => f(BigInt(a0), a1);
  var makeWrapper__pp = f => (a0, a1) => f(BigInt(a0), BigInt(a1));
  var makeWrapper__ppp = f => (a0, a1, a2) => f(BigInt(a0), BigInt(a1), BigInt(a2));
  var makeWrapper__p__ = f => (a0, a1, a2) => f(BigInt(a0), a1, a2);
  var makeWrapper___pp = f => (a0, a1, a2) => f(a0, BigInt(a1), BigInt(a2));
  var makeWrapper___pp_ = f => (a0, a1, a2, a3) => f(a0, BigInt(a1), BigInt(a2), a3);
  var makeWrapper_pp____ = f => (a0, a1, a2, a3, a4) => Number(f(BigInt(a0), a1, a2, a3, a4));
  wasmExports["free"] = makeWrapper__p(wasmExports["free"]);
  wasmExports["malloc"] = makeWrapper_pp(wasmExports["malloc"]);
  wasmExports["fflush"] = makeWrapper__p(wasmExports["fflush"]);
  wasmExports["pthread_self"] = makeWrapper_p(wasmExports["pthread_self"]);
  wasmExports["wasmfs_create_memory_backend"] = makeWrapper_p(wasmExports["wasmfs_create_memory_backend"]);
  wasmExports["wasmfs_create_file"] = makeWrapper__p_p(wasmExports["wasmfs_create_file"]);
  wasmExports["__main_argc_argv"] = makeWrapper___PP(wasmExports["__main_argc_argv"]);
  wasmExports["_emscripten_tls_init"] = makeWrapper_p(wasmExports["_emscripten_tls_init"]);
  wasmExports["emscripten_builtin_memalign"] = makeWrapper_ppp(wasmExports["emscripten_builtin_memalign"]);
  wasmExports["emscripten_stack_get_base"] = makeWrapper_p(wasmExports["emscripten_stack_get_base"]);
  wasmExports["emscripten_stack_get_end"] = makeWrapper_p(wasmExports["emscripten_stack_get_end"]);
  wasmExports["_emscripten_run_callback_on_thread"] = makeWrapper__pp_ppp(wasmExports["_emscripten_run_callback_on_thread"]);
  wasmExports["_emscripten_thread_init"] = makeWrapper__p_____(wasmExports["_emscripten_thread_init"]);
  wasmExports["__set_thread_state"] = makeWrapper__p___(wasmExports["__set_thread_state"]);
  wasmExports["emscripten_proxy_execute_queue"] = makeWrapper__p(wasmExports["emscripten_proxy_execute_queue"]);
  wasmExports["emscripten_proxy_finish"] = makeWrapper__p(wasmExports["emscripten_proxy_finish"]);
  wasmExports["_emscripten_run_js_on_main_thread_done"] = makeWrapper__pp_(wasmExports["_emscripten_run_js_on_main_thread_done"]);
  wasmExports["_emscripten_run_js_on_main_thread"] = makeWrapper___p_p_(wasmExports["_emscripten_run_js_on_main_thread"]);
  wasmExports["_emscripten_thread_free_data"] = makeWrapper__p(wasmExports["_emscripten_thread_free_data"]);
  wasmExports["_emscripten_thread_exit"] = makeWrapper__p(wasmExports["_emscripten_thread_exit"]);
  wasmExports["memalign"] = makeWrapper_ppp(wasmExports["memalign"]);
  wasmExports["setThrew"] = makeWrapper__p_(wasmExports["setThrew"]);
  wasmExports["emscripten_stack_set_limits"] = makeWrapper__pp(wasmExports["emscripten_stack_set_limits"]);
  wasmExports["emscripten_stack_get_free"] = makeWrapper_p(wasmExports["emscripten_stack_get_free"]);
  wasmExports["_emscripten_stack_restore"] = makeWrapper__p(wasmExports["_emscripten_stack_restore"]);
  wasmExports["_emscripten_stack_alloc"] = makeWrapper_pp(wasmExports["_emscripten_stack_alloc"]);
  wasmExports["emscripten_stack_get_current"] = makeWrapper_p(wasmExports["emscripten_stack_get_current"]);
  wasmExports["__cxa_decrement_exception_refcount"] = makeWrapper__p(wasmExports["__cxa_decrement_exception_refcount"]);
  wasmExports["__cxa_increment_exception_refcount"] = makeWrapper__p(wasmExports["__cxa_increment_exception_refcount"]);
  wasmExports["__get_exception_message"] = makeWrapper__ppp(wasmExports["__get_exception_message"]);
  wasmExports["__cxa_can_catch"] = makeWrapper__ppp(wasmExports["__cxa_can_catch"]);
  wasmExports["__cxa_get_exception_ptr"] = makeWrapper_pp(wasmExports["__cxa_get_exception_ptr"]);
  wasmExports["_wasmfs_fetch_get_file_url"] = makeWrapper_pp(wasmExports["_wasmfs_fetch_get_file_url"]);
  wasmExports["_wasmfs_fetch_get_chunk_size"] = makeWrapper__p(wasmExports["_wasmfs_fetch_get_chunk_size"]);
  wasmExports["_wasmfs_read_file"] = makeWrapper__ppp(wasmExports["_wasmfs_read_file"]);
  wasmExports["_wasmfs_write_file"] = makeWrapper__ppp(wasmExports["_wasmfs_write_file"]);
  wasmExports["_wasmfs_mkdir"] = makeWrapper__p_(wasmExports["_wasmfs_mkdir"]);
  wasmExports["_wasmfs_rmdir"] = makeWrapper__p(wasmExports["_wasmfs_rmdir"]);
  wasmExports["_wasmfs_open"] = makeWrapper__p__(wasmExports["_wasmfs_open"]);
  wasmExports["_wasmfs_mknod"] = makeWrapper__p__(wasmExports["_wasmfs_mknod"]);
  wasmExports["_wasmfs_unlink"] = makeWrapper__p(wasmExports["_wasmfs_unlink"]);
  wasmExports["_wasmfs_chdir"] = makeWrapper__p(wasmExports["_wasmfs_chdir"]);
  wasmExports["_wasmfs_symlink"] = makeWrapper__pp(wasmExports["_wasmfs_symlink"]);
  wasmExports["_wasmfs_readlink"] = makeWrapper__pp(wasmExports["_wasmfs_readlink"]);
  wasmExports["_wasmfs_write"] = makeWrapper___pp(wasmExports["_wasmfs_write"]);
  wasmExports["_wasmfs_pwrite"] = makeWrapper___pp_(wasmExports["_wasmfs_pwrite"]);
  wasmExports["_wasmfs_chmod"] = makeWrapper__p_(wasmExports["_wasmfs_chmod"]);
  wasmExports["_wasmfs_lchmod"] = makeWrapper__p_(wasmExports["_wasmfs_lchmod"]);
  wasmExports["_wasmfs_rename"] = makeWrapper__pp(wasmExports["_wasmfs_rename"]);
  wasmExports["_wasmfs_read"] = makeWrapper___pp(wasmExports["_wasmfs_read"]);
  wasmExports["_wasmfs_pread"] = makeWrapper___pp_(wasmExports["_wasmfs_pread"]);
  wasmExports["_wasmfs_truncate"] = makeWrapper__p_(wasmExports["_wasmfs_truncate"]);
  wasmExports["_wasmfs_mmap"] = makeWrapper_pp____(wasmExports["_wasmfs_mmap"]);
  wasmExports["_wasmfs_msync"] = makeWrapper__pp_(wasmExports["_wasmfs_msync"]);
  wasmExports["_wasmfs_munmap"] = makeWrapper__pp(wasmExports["_wasmfs_munmap"]);
  wasmExports["_wasmfs_utime"] = makeWrapper__p__(wasmExports["_wasmfs_utime"]);
  wasmExports["_wasmfs_stat"] = makeWrapper__pp(wasmExports["_wasmfs_stat"]);
  wasmExports["_wasmfs_lstat"] = makeWrapper__pp(wasmExports["_wasmfs_lstat"]);
  wasmExports["_wasmfs_mount"] = makeWrapper__pp(wasmExports["_wasmfs_mount"]);
  wasmExports["_wasmfs_identify"] = makeWrapper__p(wasmExports["_wasmfs_identify"]);
  wasmExports["_wasmfs_readdir_start"] = makeWrapper_pp(wasmExports["_wasmfs_readdir_start"]);
  wasmExports["_wasmfs_readdir_get"] = makeWrapper_pp(wasmExports["_wasmfs_readdir_get"]);
  wasmExports["_wasmfs_readdir_finish"] = makeWrapper__p(wasmExports["_wasmfs_readdir_finish"]);
  wasmExports["_wasmfs_get_cwd"] = makeWrapper_p(wasmExports["_wasmfs_get_cwd"]);
  wasmExports["wasmfs_create_jsimpl_backend"] = makeWrapper_p(wasmExports["wasmfs_create_jsimpl_backend"]);
  wasmExports["_wasmfs_opfs_record_entry"] = makeWrapper__pp_(wasmExports["_wasmfs_opfs_record_entry"]);
  wasmExports["wasmfs_unmount"] = makeWrapper__p(wasmExports["wasmfs_unmount"]);
  return wasmExports;
}

// include: postamble.js
// === Auto-generated postamble setup entry stuff ===
var calledRun;

function callMain(args = []) {
  assert(runDependencies == 0, 'cannot call main when async dependencies remain! (listen on Module["onRuntimeInitialized"])');
  assert(typeof onPreRuns === "undefined" || onPreRuns.length == 0, "cannot call main when preRun functions remain to be called");
  var entryFunction = __emscripten_proxy_main;
  // With PROXY_TO_PTHREAD make sure we keep the runtime alive until the
  // proxied main calls exit (see exitOnMainThread() for where Pop is called).
  runtimeKeepalivePush();
  args.unshift(thisProgram);
  var argc = args.length;
  var argv = stackAlloc((argc + 1) * 8);
  var argv_ptr = argv;
  for (var arg of args) {
    (growMemViews(), HEAPU64)[((argv_ptr) >> 3)] = BigInt(stringToUTF8OnStack(arg));
    argv_ptr += 8;
  }
  (growMemViews(), HEAPU64)[((argv_ptr) >> 3)] = 0n;
  try {
    var ret = entryFunction(argc, BigInt(argv));
    // if we're not running an evented main loop, it's time to exit
    exitJS(ret, /* implicit = */ true);
    return ret;
  } catch (e) {
    return handleException(e);
  }
}

function stackCheckInit() {
  // This is normally called automatically during __wasm_call_ctors but need to
  // get these values before even running any of the ctors so we call it redundantly
  // here.
  // See $establishStackSpace for the equivalent code that runs on a thread
  assert(!ENVIRONMENT_IS_PTHREAD);
  _emscripten_stack_init();
  // TODO(sbc): Move writeStackCookie to native to to avoid this.
  writeStackCookie();
}

async function run(args = programArgs) {
  assert(!calledRun);
  calledRun = true;
  if ((ENVIRONMENT_IS_PTHREAD)) {
    initRuntime();
    return;
  }
  stackCheckInit();
  preRun();
  if (runDependencies) {
    await resolveRunDependencies();
  }
  var setStatus = Module["setStatus"];
  if (setStatus) {
    setStatus("Running...");
    // Yield to the event loop to allow the browser to paint "Running..."
    await new Promise(resolve => setTimeout(resolve, 1));
    // Then we want to clear the status text, but only after the rest of this function runs.
    setTimeout(setStatus, 1, "");
  }
  if (ABORT) return;
  initRuntime();
  // No ATMAINS hooks
  Module["onRuntimeInitialized"]?.();
  consumedModuleProp("onRuntimeInitialized");
  var noInitialRun = Module["noInitialRun"] || false;
  if (!noInitialRun) callMain(args);
  postRun();
}

var wasmExports;

if ((!(ENVIRONMENT_IS_PTHREAD))) {
  // Call createWasm on startup if we are the main thread.
  // Worker threads call this once they receive the module via postMessage
  // In modularize mode the generated code is within a factory function so we
  // can use await here (since it's not top-level-await).
  wasmExports = await createWasm();
  await run();
}

// end include: postamble.js
// include: postamble_modularize.js
// In MODULARIZE mode we wrap the generated code in a factory function
// and return either the Module itself, or a promise of the module.
// Assertion for attempting to access module properties on the incoming
// moduleArg.  In the past we used this object as the prototype of the module
// and assigned properties to it, but now we return a distinct object.  This
// keeps the instance private until it is ready (i.e the promise has been
// resolved).
for (const prop of Object.keys(Module)) {
  if (!(prop in moduleArg)) {
    Object.defineProperty(moduleArg, prop, {
      configurable: true,
      get() {
        abort(`Access to module property ('${prop}') is no longer possible via the module constructor argument; Instead, use the result of the module constructor.`);
      }
    });
  }
}


    return Module;
  };
})();

// Export using a UMD style export, or ES6 exports if selected
if (typeof exports === 'object' && typeof module === 'object') {
  module.exports = createUEModule;
  // This default export looks redundant, but it allows TS to import this
  // commonjs style module.
  module.exports.default = createUEModule;
} else if (typeof define === 'function' && define['amd'])
  define([], () => createUEModule);

// Create code for detecting if we are running in a pthread.
// Normally this detection is done when the module is itself run but
// when running in MODULARIZE mode we need use this to know if we should
// run the module constructor on startup (true only for pthreads).
var isPthread = globalThis.name?.startsWith('em-pthread');

isPthread && createUEModule();

