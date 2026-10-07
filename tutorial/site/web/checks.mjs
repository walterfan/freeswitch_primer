export function declaredCheck(lesson, checkId) {
  return lesson?.checks?.find(check => check.id === checkId) || null;
}

export function checkPath(lesson, checkId) {
  if (!/^day-(0[1-9]|[12][0-9]|30)$/.test(lesson?.id || "") || !declaredCheck(lesson, checkId))
    throw new Error("check is not declared by the active lesson");
  return `/api/v1/checks/${encodeURIComponent(lesson.id)}/${encodeURIComponent(checkId)}`;
}
