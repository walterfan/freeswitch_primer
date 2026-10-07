const STORAGE_KEY = "freeswitch-tutorial-progress-v1";

export class ProgressStore {
  constructor(storage) { this.storage = storage; }
  completed() {
    try {
      const parsed = JSON.parse(this.storage.getItem(STORAGE_KEY) || "{}");
      return new Set(Array.isArray(parsed.completed) ? parsed.completed.filter(validLessonId) : []);
    } catch (_) { return new Set(); }
  }
  setCompleted(lessonId, complete) {
    if (!validLessonId(lessonId)) throw new Error("invalid lesson id");
    const completed = this.completed();
    complete ? completed.add(lessonId) : completed.delete(lessonId);
    this.storage.setItem(STORAGE_KEY, JSON.stringify({completed: [...completed].sort()}));
    return completed;
  }
}
export function validLessonId(value) { return /^day-(0[1-9]|[12][0-9]|30)$/.test(value); }
export {STORAGE_KEY};
