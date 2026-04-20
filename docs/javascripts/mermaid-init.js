document.addEventListener("DOMContentLoaded", function () {
  if (typeof mermaid === "undefined") return;

  const mermaidCodeBlocks = document.querySelectorAll("pre code.language-mermaid");
  mermaidCodeBlocks.forEach((codeBlock, index) => {
    const pre = codeBlock.parentElement;
    const wrapper = document.createElement("div");
    wrapper.className = "mermaid";
    wrapper.id = "mermaid-diagram-" + index;
    wrapper.textContent = codeBlock.textContent;
    pre.replaceWith(wrapper);
  });

  mermaid.initialize({ startOnLoad: false });
  mermaid.run();
});
