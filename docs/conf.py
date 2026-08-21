"""Sphinx configuration for the SplatIt server documentation."""

project = "SplatIt Server"
author = "oxixes"
copyright = "2026, oxixes"
release = "1.0.0"

extensions = [
    "myst_parser",
    "sphinxcontrib.mermaid",
    "sphinx_copybutton",
    "sphinx_design",
]

myst_enable_extensions = [
    "colon_fence",
    "deflist",
    "fieldlist",
    "linkify",
    "substitution",
    "tasklist",
]

myst_heading_anchors = 3

source_suffix = {
    ".md": "markdown",
    ".rst": "restructuredtext",
}

master_doc = "index"

templates_path = ["_templates"]
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

html_theme = "furo"
html_static_path = ["_static"]
html_title = "SplatIt Server"

html_theme_options = {
    "source_repository": "https://github.com/oxixes/splatit",
    "source_branch": "master",
    "source_directory": "docs/",
}

# Let the browser render the Mermaid diagrams, which keeps them selectable and
# lets them follow the light and dark themes.
mermaid_output_format = "raw"
mermaid_version = "10.9.1"

# Mermaid defaults to its light palette, which looks wrong on a dark page. Pick
# the palette from the theme Furo is showing, and redraw when the reader toggles
# it. The extension calls mermaid.run() on window load, after this has set the
# theme and stashed each diagram's source.
#
# This string replaces the whole body of the init module, including the import
# the extension would otherwise emit, so bring the import along. Skip the ELK
# layout registration it also emits: registerLayoutLoaders only exists in
# Mermaid 11 and up, and calling it on 10.x throws and kills the module.
_MERMAID_ESM = (
    "https://cdn.jsdelivr.net/npm/mermaid@" + mermaid_version + "/dist/mermaid.esm.min.mjs"
)

_MERMAID_THEME_JS = """
(function () {
  function currentTheme() {
    var chosen = (document.body && document.body.dataset.theme) || "auto";
    if (chosen === "auto") {
      return window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "default";
    }
    return chosen === "dark" ? "dark" : "default";
  }

  function diagrams() {
    return Array.prototype.slice.call(document.querySelectorAll(".mermaid"));
  }

  function applyTheme() {
    mermaid.initialize({ startOnLoad: false, theme: currentTheme() });
  }

  function redraw() {
    var nodes = diagrams();
    nodes.forEach(function (el) {
      if (el.dataset.mermaidSource) {
        el.removeAttribute("data-processed");
        el.textContent = el.dataset.mermaidSource;
      }
    });
    applyTheme();
    mermaid.run({ nodes: nodes });
  }

  document.addEventListener("DOMContentLoaded", function () {
    diagrams().forEach(function (el) {
      if (!el.dataset.mermaidSource) {
        el.dataset.mermaidSource = el.textContent;
      }
    });
    applyTheme();
    new MutationObserver(redraw).observe(document.body, {
      attributes: true,
      attributeFilter: ["data-theme"],
    });
  });
})();
"""

mermaid_init_js = 'import mermaid from "' + _MERMAID_ESM + '";\n' + _MERMAID_THEME_JS

copybutton_prompt_text = r">>> |\.\.\. |\$ |PS> "
copybutton_prompt_is_regexp = True
