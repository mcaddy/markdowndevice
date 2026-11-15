/* Editor enhancement (ES5). Progressive: if this fails to load or run, the
 * page is still a working textarea + Save form. Adds a live preview pane and
 * a small "unsaved changes" indicator, plus Ctrl/Cmd+S to save. */
(function () {
  "use strict";

  var src = document.getElementById("src");
  var preview = document.getElementById("preview");
  var status = document.getElementById("status");
  if (!src || !preview) return;

  var saved = src.value;
  var timer = null;

  function renderNow() {
    if (typeof window.renderMarkdown === "function") {
      preview.innerHTML = window.renderMarkdown(src.value);
    } else {
      /* fallback: show as preformatted text */
      preview.textContent = src.value;
    }
  }

  function markDirty() {
    if (!status) return;
    status.textContent = (src.value !== saved) ? "unsaved changes" : "";
  }

  function onInput() {
    if (timer) clearTimeout(timer);
    timer = setTimeout(renderNow, 150);
    markDirty();
  }

  if (src.addEventListener) {
    src.addEventListener("input", onInput, false);
    src.addEventListener("keyup", onInput, false); /* older browsers */
  } else if (src.attachEvent) {
    src.attachEvent("onkeyup", onInput);
  }

  /* Ctrl/Cmd+S submits the form instead of triggering the browser save. */
  document.addEventListener && document.addEventListener("keydown", function (e) {
    if ((e.ctrlKey || e.metaKey) && (e.key === "s" || e.keyCode === 83)) {
      if (e.preventDefault) e.preventDefault();
      var form = src.form;
      if (form) form.submit();
    }
  }, false);

  renderNow();
  markDirty();
})();
