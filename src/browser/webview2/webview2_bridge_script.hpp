#ifndef NATIVEWEB_WEBVIEW2_BRIDGE_SCRIPT_HPP_INCLUDED
#define NATIVEWEB_WEBVIEW2_BRIDGE_SCRIPT_HPP_INCLUDED

namespace nativeweb {
namespace detail {

static const wchar_t kWebView2BridgeScript[] = LR"NATIVEWEBJS(
(function(global) {
  if (!global.chrome || !global.chrome.webview) return;

  const pending = new Map();
  const subscriptions = new Map();
  const queuedUntilSession = [];
  let documentSession = null;
  let nextRequestId = 1;
  let nextSubscriptionId = 1;

  const errorText = (error) => {
    if (!error) return "JavaScript error";
    if (typeof error === "string") return error;
    if (error.message) return String(error.message);
    return String(error);
  };

  // Keep 24KiB chunks (multiple of three) so concatenated base64 pieces
  // never contain intermediate '=' padding. Avoid huge String.fromCharCode
  // argument lists on large TypedArrays.
  const bytesToBase64 = (bytes) => {
    let result = "";
    const chunkSize = 24576;
    for (let start=0;start<bytes.length;start+=chunkSize) {
      const chunk=bytes.subarray(start,start+chunkSize);
      result+=btoa(String.fromCharCode.apply(null,chunk));
    }
    return result;
  };
  const base64ToBuffer = (encoded) => {
    const binary=atob(encoded);
    const bytes=new Uint8Array(binary.length);
    for(let i=0;i<binary.length;++i) bytes[i]=binary.charCodeAt(i);
    return bytes.buffer;
  };

  const encode = (value) => {
    if (value instanceof ArrayBuffer) {
      return { __nativeweb_binary_base64: bytesToBase64(new Uint8Array(value)) };
    }

    if (ArrayBuffer.isView(value)) {
      return {
        __nativeweb_binary_base64: bytesToBase64(
          new Uint8Array(value.buffer, value.byteOffset, value.byteLength)
        )
      };
    }

    if (!value || typeof value !== "object") return value;

    if (value.__nativeweb_object === true || value.__nativewebObjectId) {
      return {
        __nativeweb_object: true,
        id: String(value.id || value.__nativewebObjectId),
        type: String(value.type || value.__nativewebType || "")
      };
    }

    if (Array.isArray(value)) return value.map(encode);

    const output = {};
    for (const key of Object.keys(value)) output[key] = encode(value[key]);
    return output;
  };

  const startRequest = (method, args) => {
    if (global !== global.top)
      return { id:null, promise:Promise.reject(
        new Error("permission_denied: not main frame")) };
    if (typeof method !== "string") {
      return { id:null, promise:Promise.reject(
        new TypeError("xytron.invoke(method, ...args) requires a method string")
      ) };
    }

    const id = String(nextRequestId++);
    const promise = new Promise((resolve, reject) => {
      pending.set(id, { resolve, reject });
    });

    const transmit = () => {
      if (!pending.has(id)) return;
      global.chrome.webview.postMessage(
        encode({ type: "request", id, method, args, session: documentSession })
      );
    };
    if (documentSession) transmit();
    else if (queuedUntilSession.length < 256) queuedUntilSession.push(transmit);
    else {
      pending.delete(id);
      return Promise.reject(new Error("session_unavailable: pending queue full"));
    }

    return { id, promise };
  };

  const invokeRaw = (method, ...args) => startRequest(method, args).promise;

  const cancelRaw = (id, code) => {
    const key=String(id);
    const item=pending.get(key);
    if (!item) return false;
    pending.delete(key);
    const reason=code === "request_timeout" ? "request_timeout" : "request_cancelled";
    item.reject(new Error(reason + ": JavaScript request cancelled"));
    if (documentSession) {
      global.chrome.webview.postMessage(
        encode({ type:"cancel", id:key, session:documentSession })
      );
    }
    return true;
  };

  // timeoutMs is a local JS Promise deadline; an already-running native
  // method or managed subprocess cannot be forcibly interrupted safely.
  const invokeWithOptions = (method, options, ...args) => {
    const opts = options == null ? {} : options;
    if (typeof opts !== "object")
      return Promise.reject(new TypeError("options must be an object"));
    const timeoutMs=opts.timeoutMs;
    if (timeoutMs !== undefined &&
        (!Number.isFinite(timeoutMs) || timeoutMs <= 0 ||
         timeoutMs > 60000 || !Number.isInteger(timeoutMs)))
      return Promise.reject(new RangeError("timeoutMs must be 1..60000"));
    const signal=opts.signal;
    if (signal != null &&
        (typeof signal.addEventListener !== "function" ||
         typeof signal.removeEventListener !== "function" ||
         typeof signal.aborted !== "boolean"))
      return Promise.reject(new TypeError("signal must be an AbortSignal"));
    if (signal && signal.aborted)
      return Promise.reject(new Error("request_cancelled: signal already aborted"));
    const { id, promise } = startRequest(method, args);
    if (!id) return promise;
    const onAbort=() => cancelRaw(id,"request_cancelled");
    if(signal) signal.addEventListener("abort",onAbort,{once:true});
    const timer=timeoutMs === undefined ? null :
        setTimeout(() => cancelRaw(id,"request_timeout"),timeoutMs);
    const cleanup=() => {
      if(timer !== null) clearTimeout(timer);
      if(signal) signal.removeEventListener("abort",onAbort);
    };
    promise.then(cleanup,cleanup);
    if (signal && signal.aborted) onAbort();
    return promise;
  };

)NATIVEWEBJS"
LR"NATIVEWEBJS(
  const createNativeObject = (value) => {
    const target = {
      __nativeweb_object: true,
      id: String(value.id),
      type: String(value.type || "")
    };

    return new Proxy(target, {
      get(object, property) {
        if (property === "then") return undefined;
        if (property === "__nativewebObjectId") return object.id;
        if (property === "__nativewebType") return object.type;

        if (property === "dispose") {
          return () =>
            invokeRaw("__native_object.release", object.id, object.type);
        }

        if (property in object) return object[property];
        if (typeof property !== "string") return undefined;

        return (...args) =>
          invokeRaw(
            "__native_object.call",
            object.id,
            object.type,
            property,
            ...args
          );
      }
    });
  };

  const decode = (value) => {
    if (!value || typeof value !== "object") return value;

    if (Object.prototype.hasOwnProperty.call(value, "__nativeweb_binary_base64")) {
      return base64ToBuffer(value.__nativeweb_binary_base64);
    }
    // Accept legacy numeric array framing until old clients are retired.
    if (Object.prototype.hasOwnProperty.call(value, "__nativeweb_binary")) {
      return Uint8Array.from(value.__nativeweb_binary).buffer;
    }

    if (value.__nativeweb_object === true) {
      return createNativeObject(value);
    }

    if (Array.isArray(value)) return value.map(decode);

    for (const key of Object.keys(value)) value[key] = decode(value[key]);
    return value;
  };

  const resolveDottedFunction = (method) => {
    const parts = String(method).split(".");
    let receiver = global;

    for (let index = 0; index < parts.length - 1; ++index) {
      receiver = receiver && receiver[parts[index]];
    }

    const name = parts[parts.length - 1];
    const fn = receiver && receiver[name];
    if (typeof fn !== "function") return null;
    return { receiver, fn };
  };

  const postError = (id, code, message) => {
    global.chrome.webview.postMessage(
      encode({
        type: "error",
        id: String(id),
        code,
        message: String(message),
        session: documentSession
      })
    );
  };

  const handleNativeRequest = (message) => {
    const resolved = resolveDottedFunction(message.method);

    if (!resolved) {
      postError(
        message.id,
        "js_method_not_found",
        "JavaScript method not found: " + String(message.method)
      );
      return;
    }

    let result;

    try {
      result = resolved.fn.apply(resolved.receiver, message.args || []);
    } catch (error) {
      postError(message.id, "js_exception", errorText(error));
      return;
    }

    Promise.resolve(result).then(
      (value) => {
        global.chrome.webview.postMessage(
          encode({ type: "response", id: String(message.id), value, session: documentSession })
        );
      },
      (error) => {
        postError(message.id, "js_promise_rejected", errorText(error));
      }
    );
  };

  global.chrome.webview.addEventListener("message", (event) => {
    const message = decode(event.data);
    if (!message || typeof message !== "object") return;

    if (message.type === "nativeweb_session") {
      if (global !== global.top || documentSession ||
          typeof message.token !== "string" ||
          !/^[0-9a-f]{48}$/.test(message.token)) return;
      documentSession = message.token;
      for (const transmit of queuedUntilSession.splice(0)) transmit();
      return;
    }

    if (message.type === "response" || message.type === "error") {
      const id = String(message.id);
      const item = pending.get(id);
      if (!item) return;

      pending.delete(id);

      if (message.type === "response") {
        item.resolve(message.value);
      } else {
        item.reject(
          new Error(String(message.code) + ": " + String(message.message))
        );
      }
      return;
    }

    if (message.type === "event") {
      for (const subscription of subscriptions.values()) {
        if (subscription.eventName === message.event) {
          try {
            subscription.callback(message.payload);
          } catch (_) {
          }
        }
      }
      return;
    }

    if (message.type === "request") {
      handleNativeRequest(message);
    }
  });

  const postMessage = (channel, payload = null) => {
    if (global !== global.top)
      throw new Error("permission_denied: not main frame");
    if (typeof channel !== "string" || !channel || channel.length > 256)
      throw new TypeError("Message channel must be 1..256 characters");
    const transmit = () => global.chrome.webview.postMessage(encode({
      type: "event", event: channel, payload, session: documentSession
    }));
    if (documentSession) transmit();
    else if (queuedUntilSession.length < 256) queuedUntilSession.push(transmit);
    else throw new Error("session_unavailable: pending queue full");
  };

  const onRaw = (eventName, callback) => {
    if (typeof eventName !== "string" || typeof callback !== "function") {
      throw new TypeError(
        "xytron.on(eventName, callback) requires a string and function"
      );
    }

    const id = String(nextSubscriptionId++);
    subscriptions.set(id, { eventName, callback });
    return id;
  };

  const offRaw = (id) => subscriptions.delete(String(id));

  const native = { invokeRaw, invokeWithOptions, onRaw, offRaw };
  native.invoke = invokeRaw;
  native.on = onRaw;
  native.off = offRaw;
  native.postMessage = postMessage;
  native.onMessage = onRaw;
  native.offMessage = offRaw;

  Object.defineProperty(global, "native", {
    value: native,
    writable: false,
    configurable: false,
    enumerable: true
  });

  const namespace = (path) =>
    new Proxy(function() {}, {
      get(_target, property) {
        if (property === "then") return undefined;
        if (path === "" && property === "invoke") return invokeRaw;
        if (path === "" && property === "invokeWithOptions") return invokeWithOptions;
        if (path === "" && property === "on") return onRaw;
        if (path === "" && property === "postMessage") return postMessage;
        if (path === "" && property === "onMessage") return onRaw;
        if (path === "" && property === "offMessage") return offRaw;
        if (path === "" && property === "off") return offRaw;
        if (typeof property !== "string") return undefined;

        const next = path ? path + "." + property : property;
        return namespace(next);
      },

      apply(_target, _this, args) {
        if (!path) throw new TypeError("xytron root is not callable");
        return invokeRaw(path, ...args);
      }
    });

  Object.defineProperty(global, "xytron", {
    value: namespace(""),
    writable: false,
    configurable: false,
    enumerable: true
  });
})(globalThis);
)NATIVEWEBJS";

} // namespace detail
} // namespace nativeweb

#endif
