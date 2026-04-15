# Editor Overview

This document describes the current GUI editor architecture and workflow.

## Shell

The editor is built around `EditorOverlay`, which owns the Dear ImGui context,
top-level docking layout, menu bar, transient notices, and runtime input
routing.

The default shell exposes these panels:

- `Project`: resource browser rooted at `resources/`
- `Hierarchy`: actor list and actor-level structure editing
- `Inspector`: component and property editing for the selected actor
- `Viewport`: embedded runtime output plus play / pause / stop transport
- `Runtime`: lightweight status panel for mode, zoom, actor count, and dirty
  state

## Project Panel

The `Project` panel is a visual browser for the live `resources/` tree.

It currently supports:

- browsing directories and common asset types
- opening `.scene` files into the editor session
- launching configured external editors for scenes, templates, Lua scripts,
  images, audio, fonts, and configs
- asset-type-aware icon styling for common resource categories

The project browser is intentionally thin: it reflects the filesystem directly
instead of relying on a separate asset database or import cache.

## Hierarchy + Inspector

`Hierarchy` and `Inspector` are the authoring surface for scene data.

Hierarchy focuses on actor-level structure:

- select actor
- create empty actor
- create actor from template
- duplicate actor
- delete actor
- rename actor

Inspector focuses on component-level structure and scalar properties:

- rename actor
- add component
- delete component
- rename component key
- change component type
- edit component scalar properties

Both panels operate on the current active `SceneDocument`.

## Scene Documents

The editor session is managed by `EditorSceneSession`.

It owns two kinds of scene document:

- authoring document: the authoritative editor cache used for save-to-disk and
  edit-mode preview
- play document: a temporary sandbox copy used only while play mode is active

This keeps authoring state separate from temporary simulation state.

## Save Policy

Scene files are not rewritten on every GUI interaction.

The current save behavior is:

- load `.scene` into the authoring document on open
- keep GUI edits in memory
- write the authoring document back on explicit save
- also save the authoring document on scene switch and editor shutdown
- never save the temporary play document

## Runtime Viewport

The runtime is rendered to an offscreen SDL texture and displayed inside the
`Viewport` panel through ImGui.

The viewport also handles:

- floating transport controls for play / pause / stop
- runtime input focus rules
- embedded mode badge such as `EDIT`, `PLAYING`, or `PAUSED`

During play mode, keyboard and mouse input are forwarded to runtime only when
the viewport has focus.

## Runtime Scene Editing

The editor now supports live runtime scene editing in play mode.

The shared edit protocol is:

- editor panels emit `SceneEditCommand`
- each command contains one or more `SceneMutation`
- the active `SceneDocument` applies the mutations first
- `EditorSceneSession` decides how runtime should react

Behavior by mode:

- edit mode: GUI edits mutate the authoring document, and the runtime preview is
  refreshed by reloading the full scene asset on the next frame boundary
- play mode: GUI edits mutate the play document and are also applied
  incrementally to the live runtime

Supported mutation categories currently include:

- create / delete actor
- rename actor
- add / delete component
- rename component key
- change component type
- change component property

## Stable Actor Identity

To make live runtime edits possible, actors now carry a stable editor-side UID.

That UID exists in three places during an editor session:

- scene document actor records
- runtime actor instances
- scene edit commands

This lets play-mode mutations target the correct live actor even though runtime
actor IDs are allocated separately.

## Current Limits

The current implementation is intentionally incremental.

- Edit mode still mirrors by full scene reload instead of per-mutation runtime
  patching.
- Some play-mode component edits still rebuild the touched runtime component.
- Builtin runtime components such as `Rigidbody` and `ParticleSystem` are more
  stateful, so they still fall back to rebuild for many property changes.
- Save is disabled in play mode because sandbox edits are temporary by design.
