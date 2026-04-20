# Actor API

The `Actor` type is both:

- an instance class for live actors
- a static namespace for actor lookup / creation helpers

## Instance methods

### Identity and hierarchy

| Method | Description |
| --- | --- |
| `actor:GetName()` | Returns the actor name |
| `actor:GetUID()` | Returns the stable runtime/editor actor UID |
| `actor:SetParent(parent_actor)` | Reparents the actor in the live runtime world |
| `actor:GetChildCount()` | Returns the number of direct children |
| `actor:GetChildren()` | Returns direct children as a Lua array |

### Component lookup

| Method | Description |
| --- | --- |
| `actor:GetComponentByKey(key)` | Returns the component with that key, or `nil` |
| `actor:GetComponent(type_name)` | Returns the first component of that type, or `nil` |
| `actor:GetComponents(type_name)` | Returns all components of that type as a Lua array |
| `actor:GetComponentInChildren(type_name)` | Returns the first matching component on self or descendants |
| `actor:GetComponentsInChildren(type_name)` | Returns all matching components on self and descendants |

### Component mutation

| Method | Description |
| --- | --- |
| `actor:AddComponent(type_name)` | Adds a component instance and returns it |
| `actor:RemoveComponent(component_ref)` | Removes a component instance |

## Static `Actor` namespace

| Function | Description |
| --- | --- |
| `Actor.Instantiate(template_name)` | Spawns an actor from a template |
| `Actor.Destroy(actor)` | Requests destroying the actor at frame end |
| `Actor.Find(name)` | Returns the first actor with the given name |
| `Actor.FindAll(name)` | Returns all matching actors as a Lua array |

## Notes

- `GetChildren()` only returns direct children.
- `GetComponentInChildren()` and `GetComponentsInChildren()` include `self`
  first, then descendants.
- Runtime child queries are derived from parent links on demand; there is no
  separate permanent children tree in the runtime core.
