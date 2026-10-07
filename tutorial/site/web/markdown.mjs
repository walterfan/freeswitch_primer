import MarkdownIt from "./vendor/markdown-it/markdown-it-15.0.1.esm.min.mjs";

export const MARKDOWN_IT_VERSION = "15.0.1";

// Lesson Markdown is repository-owned content, but HTML remains disabled so a
// malformed lesson cannot inject active markup into the teaching application.
const renderer = new MarkdownIt({
  html: false,
  linkify: false,
  typographer: false,
});

export function renderMarkdown(source) {
  if (typeof source !== "string") throw new TypeError("Markdown source must be a string");
  return renderer.render(source);
}
