# Menus, settings and presentation styling

Start with [screens.cppm](screens.cppm).

NativeMenuScreen owns one menu/lobby presentation state. settings.cppm owns shared settings actions; theme.cppm owns generated style application/resource retention. skin.cppm is the pure source-pixel metadata contract.

Menu theming consumes the prepared session or public menu catalogue. The rendering loop does not load library archives or resolve their catalogues.

Dependencies: Application/session interfaces, localisation, Rml runtime and content. Pure skin metadata has a separate renderer-free target.

Validation: Existing menu and input/semantics cases; native/browser menu and lobby journeys remain manual smoke.
