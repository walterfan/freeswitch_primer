export const SIP_STATES = Object.freeze([
  "idle",
  "connecting",
  "registering",
  "registered",
  "incoming",
  "ringing",
  "established",
  "terminating",
  "terminated",
  "failed",
]);

const ALLOWED_TRANSITIONS = Object.freeze({
  idle: ["connecting", "failed"],
  connecting: ["registering", "terminating", "failed"],
  registering: ["registered", "terminating", "failed"],
  registered: ["incoming", "ringing", "terminating", "failed"],
  incoming: ["ringing", "established", "terminating", "terminated", "failed"],
  ringing: ["established", "terminating", "terminated", "failed"],
  established: ["terminating", "terminated", "failed"],
  terminating: ["terminated", "failed"],
  terminated: ["idle", "connecting", "registered"],
  failed: ["idle", "connecting", "terminating"],
});

export class SipStateMachine {
  #state = "idle";
  #listeners = new Set();

  get state() { return this.#state; }

  onChange(listener) {
    if (typeof listener !== "function") throw new TypeError("state listener must be a function");
    this.#listeners.add(listener);
    return () => this.#listeners.delete(listener);
  }

  transition(next, detail = "") {
    if (!SIP_STATES.includes(next)) throw new Error(`unknown SIP state: ${next}`);
    if (!ALLOWED_TRANSITIONS[this.#state].includes(next)) {
      throw new Error(`invalid SIP transition: ${this.#state} -> ${next}`);
    }
    const previous = this.#state;
    this.#state = next;
    const event = Object.freeze({previous, state: next, detail: String(detail)});
    for (const listener of this.#listeners) listener(event);
    return event;
  }
}
