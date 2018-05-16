/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
var idleCallbackHandle;


function _idleCallbackHandler() {
  dump('staktrace: got idle callback (' + idleCallbackHandle + ')\n');
  content.window.cancelIdleCallback(idleCallbackHandle);
  sendAsyncMessage("PageLoader:IdleCallbackReceived", {});
}

function setIdleCallback() {
  idleCallbackHandle = content.window.requestIdleCallback(_idleCallbackHandler);
  dump('staktrace: registered for idle callback ' + idleCallbackHandle + '\n');
  sendAsyncMessage("PageLoader:IdleCallbackSet", {});
}

function contentLoadHandlerCallback(cb) {
  function _handler(e) {
    if (e.originalTarget.defaultView == content) {
      content.wrappedJSObject.tpRecordTime = function(t, s, n) {
        sendAsyncMessage("PageLoader:RecordTime", {time: t, startTime: s, testName: n});
      };
      content.setTimeout(cb, 0);
      content.setTimeout(setIdleCallback, 0);
    }
  }
  return _handler;
}
