---
name: pocketengine-scene-authoring
description: Inspect and safely author PocketEngine 2D scenes through the PocketEngine MCP tools. Use when Codex or Claude Code needs to explain the open editor scene, inspect actors and components, find project assets, diagnose scene structure, or plan scene edits without directly editing .scene files.
---

# PocketEngine Scene Authoring

Treat the editor's `SceneDocument` and runtime mirror as the source of truth. Use PocketEngine MCP tools instead of parsing or editing `.scene` files directly.

## Read-only workflow

1. Call `get_editor_state` to learn the project, open scene, editor mode, selection, and dirty state.
2. Call `get_current_scene` for the actor hierarchy and component summary.
3. Call `inspect_actor` before making claims about an actor's properties, transforms, template inheritance, or physics hierarchy.
4. Call `list_component_types` before recommending component names or property types.
5. Call `search_assets` before referring to a project asset.
6. Answer from tool results. Preserve stable actor UIDs and component keys exactly; never guess them.

Phase 1 is read-only. Do not edit `.scene` files, invoke filesystem write tools, or claim that a scene change was applied. If the user asks for a change, describe the proposed change and state that ChangeSet authoring becomes available in Phase 2.

## Safety rules

- Keep all asset queries within the current project root.
- Distinguish authoring state from runtime-only state and say which one a conclusion uses.
- Treat `dirty: true` as unsaved editor state, not as a failure.
- Respect truncation markers. Narrow the query or inspect specific actor UIDs instead of assuming omitted actors do not exist.
- On tool failure, report the stable error code and a useful next step. Do not repeatedly retry the same invalid request.

## Future write workflow

When write tools become available, keep one main ChangeSet per user request. Inspect targets and schemas first, then validate and preview before asking the user to commit. Never bypass the editor transaction path or repeat a rejected ChangeSet.
