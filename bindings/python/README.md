# Python export authoring slice

The public `@xytron.export` marker and the *local* `discover_exports`
introspection helper live in `bindings/python/xytron`. The helper
enumerates explicitly decorated public functions/classes, and public
declared class methods/properties; private methods/aliases are excluded.
Use `python -m unittest discover -s bindings/python/tests -v`.

This does NOT launch Python as a supervised Main/Plugin backend, process
streaming requests or authorize JS calls. JS/native transport, coercion,
async callbacks, module identity, validation and packaging are future
A1-BIND-06/B3 release gates.
