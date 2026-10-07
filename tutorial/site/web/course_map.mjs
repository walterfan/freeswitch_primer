export function groupLessonsByPhase(manifest) {
  if (!manifest || !Array.isArray(manifest.phases) || !Array.isArray(manifest.lessons)) {
    throw new TypeError("course manifest must contain phases and lessons");
  }
  const grouped = new Map(manifest.phases.map(phase => [phase.id, []]));
  for (const lesson of manifest.lessons) {
    const lessons = grouped.get(lesson.phase);
    if (!lessons) throw new Error(`lesson uses unknown phase: ${lesson.phase}`);
    lessons.push(lesson);
  }
  return grouped;
}

export function selectLesson(manifest, requestedId) {
  if (!manifest?.lessons?.length) throw new Error("course manifest has no lessons");
  return manifest.lessons.find(lesson => lesson.id === requestedId) || manifest.lessons[0];
}

export function lessonHash(lessonId) {
  if (!/^day-(0[1-9]|[12][0-9]|30)$/.test(lessonId)) {
    throw new Error("invalid lesson id");
  }
  return `#${lessonId}`;
}
