/* vim: set ts=2 sw=2 sts=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const EXPORTED_SYMBOLS = ["ASRouterChild"];

const { MESSAGE_TYPE_LIST, MESSAGE_TYPE_HASH: msg } = ChromeUtils.import(
  "resource://activity-stream/common/ActorConstants.jsm"
);
const { ASRouterTelemetry } = ChromeUtils.import(
  "resource://activity-stream/lib/ASRouterTelemetry.jsm"
);

const VALID_TYPES = new Set(MESSAGE_TYPE_LIST);

class ASRouterChild extends JSWindowActorChild {
  constructor() {
    super();
    this.observers = new Set();
    this.telemetry = new ASRouterTelemetry({ isParentProcess: false });
  }

  didDestroy() {
    this.observers.clear();
  }

  handleEvent(event) {
    switch (event.type) {
      case "DOMWindowCreated": {
        const window = this.contentWindow;
        Cu.exportFunction(this.asRouterMessage.bind(this), window, {
          defineAs: "ASRouterMessage",
        });
        Cu.exportFunction(this.addParentListener.bind(this), window, {
          defineAs: "ASRouterAddParentListener",
        });
        Cu.exportFunction(this.removeParentListener.bind(this), window, {
          defineAs: "ASRouterRemoveParentListener",
        });
        break;
      }
    }
  }

  addParentListener(listener) {
    this.observers.add(listener);
  }

  removeParentListener(listener) {
    this.observers.delete(listener);
  }

  receiveMessage({ name, data }) {
    switch (name) {
      case "EnterSnippetsPreviewMode":
      case "UpdateAdminState":
      case "ClearProviders":
      case "ClearMessages": {
        this.observers.forEach(listener => {
          let result = Cu.cloneInto(
            {
              type: name,
              data,
            },
            this.contentWindow
          );
          listener(result);
        });
        break;
      }
    }
  }

  wrapPromise(promise) {
    return new this.contentWindow.Promise((resolve, reject) =>
      promise.then(resolve, reject)
    );
  }

  sendQuery(aName, aData = null) {
    return this.wrapPromise(
      new Promise(resolve => {
        super.sendQuery(aName, aData).then(result => {
          resolve(Cu.cloneInto(result, this.contentWindow));
        });
      })
    );
  }

  asRouterMessage({ type, data }) {
    if (VALID_TYPES.has(type)) {
      switch (type) {
        // these messages are telemetry and can be done client side
        case msg.AS_ROUTER_TELEMETRY_USER_EVENT:
        case msg.TOOLBAR_BADGE_TELEMETRY:
        case msg.TOOLBAR_PANEL_TELEMETRY:
        case msg.MOMENTS_PAGE_TELEMETRY:
        case msg.DOORHANGER_TELEMETRY: {
          return this.telemetry.sendTelemetry(data);
        }
        // these messages don't need a repsonse
        case msg.DISABLE_PROVIDER:
        case msg.ENABLE_PROVIDER:
        case msg.EXPIRE_QUERY_CACHE:
        case msg.FORCE_WHATSNEW_PANEL:
        case msg.CLOSE_WHATSNEW_PANEL:
        case msg.IMPRESSION:
        case msg.RESET_PROVIDER_PREF:
        case msg.SET_PROVIDER_USER_PREF:
        case msg.USER_ACTION: {
          return this.sendAsyncMessage(type, data);
        }
        default: {
          // these messages need a response
          return this.sendQuery(type, data);
        }
      }
    }
    throw new Error(`Unexpected type "${type}"`);
  }
}
