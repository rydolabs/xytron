#ifndef NATIVEWEB_WKWEBVIEW_BRIDGE_SCRIPT_HPP_INCLUDED
#define NATIVEWEB_WKWEBVIEW_BRIDGE_SCRIPT_HPP_INCLUDED

namespace nativeweb { namespace detail {

// Executed only in the top frame at document start; the session is delivered
// after the native main-frame navigation callback. Never trust this script's
// own claims about origin or frame identity on the native side.
static const char kWkWebViewBridgeScript[] = R"XYTRON_JS(
(() => {
  'use strict';
  if (globalThis !== globalThis.top) return;
  const subscribers = new Map();
  const outstanding = new Map();
  const pending = [];
  const nativeHandlers = new Map();
  let session = null, nextId = 1, nextSubscriber = 1;

  function encode(value) {
    if (value instanceof ArrayBuffer)
      return {__nativeweb_binary: Array.from(new Uint8Array(value))};
    if (ArrayBuffer.isView(value))
      return {__nativeweb_binary: Array.from(
        new Uint8Array(value.buffer, value.byteOffset, value.byteLength))};
    if (Array.isArray(value)) return value.map(encode);
    if (value && typeof value === 'object') {
      const obj = {};
      for (const key of Object.keys(value)) obj[key] = encode(value[key]);
      return obj;
    }
    return value;
  }
  function decode(value) {
    if (Array.isArray(value)) return value.map(decode);
    if (value && typeof value === 'object') {
      if (Array.isArray(value.__nativeweb_binary))
        return Uint8Array.from(value.__nativeweb_binary).buffer;
      const obj = {};
      for (const key of Object.keys(value)) obj[key] = decode(value[key]);
      return obj;
    }
    return value;
  }
  function send(payload) {
    const transmit = () => globalThis.webkit.messageHandlers.xytron
      .postMessage(JSON.stringify(encode({...payload, session})));
    if (session) return transmit();
    if (pending.length >= 256) throw new Error('session_unavailable');
    pending.push(transmit);
  }
  function invoke(method, ...args) {
    if (typeof method !== 'string' || !method || method.length > 256)
      return Promise.reject(new Error('invalid_method'));
    const id = String(nextId++);
    return new Promise((resolve, reject) => {
      outstanding.set(id, {resolve, reject});
      try { send({type:'request', id, method, args}); }
      catch (error) { outstanding.delete(id); reject(error); }
    });
  }
  function postMessage(channel, payload = null) {
    if (typeof channel !== 'string' || !channel || channel.length > 256)
      throw new TypeError('invalid_message_channel');
    send({type:'event', event: channel, payload});
  }
  const onMessage = (event, callback) => {
    if (typeof event !== 'string' || typeof callback !== 'function')
      throw new TypeError('Invalid message subscription');
    const id = String(nextSubscriber++);
    subscribers.set(id, {event, callback});
    return id;
  };
  const offMessage = id => subscribers.delete(String(id));
  function expose(method, handler) {
    if (typeof method !== 'string' || typeof handler !== 'function')
      throw new TypeError('Invalid JS handler');
    nativeHandlers.set(method, handler);
  }
  Object.defineProperty(globalThis, '__xytronReceive', {
    configurable:false, value: message => {
      const payload = decode(message);
      if (payload.type === 'nativeweb_session') {
        if (session || typeof payload.token !== 'string' ||
            !/^[0-9a-f]{48}$/.test(payload.token)) return;
        session = payload.token;
        for (const transmit of pending.splice(0)) transmit();
        return;
      }
      if (!session) return;
      if (payload.type === 'response' || payload.type === 'error') {
        const promise = outstanding.get(String(payload.id));
        if (!promise) return;
        outstanding.delete(String(payload.id));
        if (payload.type === 'error')
          promise.reject(new Error(payload.code + ': ' + payload.message));
        else promise.resolve(payload.value);
      } else if (payload.type === 'event') {
        for (const subscriber of subscribers.values()) {
          if (subscriber.event === payload.event) {
            try { subscriber.callback(payload.payload); }
            catch (error) { console.error(error); }
          }
        }
      } else if (payload.type === 'request') {
        const handler = nativeHandlers.get(payload.method);
        Promise.resolve().then(() => {
          if (!handler) throw new Error('method_not_found');
          return handler(...(payload.args || []));
        }).then(value =>
          send({type:'response',id:String(payload.id),value}),
          error => send({type:'error',id:String(payload.id),
            code:'js_error',message:String(error)}));
      }
    }
  });
  const api = {invoke, postMessage, onMessage, offMessage, expose,
               on:onMessage, off:offMessage};
  Object.defineProperty(globalThis, 'xytron', {
    configurable:false, value: new Proxy(function(){}, {
      get(_target, prop) {
        if (prop === 'then') return undefined;
        if (Object.prototype.hasOwnProperty.call(api, prop)) return api[prop];
        if (typeof prop !== 'string') return undefined;
        return new Proxy(function(){}, {
          get(_fn, child) {
            if (child === 'then') return undefined;
            if (typeof child !== 'string') return undefined;
            return (...args) => invoke(prop + '.' + child, ...args);
          },
          apply(_fn, _this, args) { return invoke(prop, ...args); }
        });
      }
    }), writable:false
  });
})();
)XYTRON_JS";

}} // namespace nativeweb::detail
#endif
