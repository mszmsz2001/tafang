# AI Collaboration Rules

This project uses a Blueprint + C++ collaboration workflow optimized for low reflection churn and predictable testing.

## Working Agreement

1. Confirm Blueprint variables, component references, function entry points, and naming before adding new reflected C++ APIs.
2. Prefer the user creating or confirming Blueprint variables/placeholders first; then implement C++ against that confirmed shape.
3. Prefer reusing existing `UFUNCTION`/`UPROPERTY` surfaces. Add new reflected declarations only when necessary.
4. Prefer changing `.cpp` implementation over `.h` declarations whenever possible.
5. Ask for Blueprint screenshots or exact variable names when Blueprint structure is unclear instead of guessing for long.
6. Test one narrow slice at a time: read state, mutate state, then UI refresh.

## Unreal-Specific Defaults

- Expect editor restart or closed-editor rebuilds after reflected API changes.
- Keep Blueprint widgets and graphs as presentation/integration layers when the interface already exists.
- Keep gameplay rules, validation, and state mutation in C++ once the interface is stable.
- Align names with existing Blueprint assets unless there is an explicit rename task.

## Feature Workflow

1. Define the feature as Blueprint inputs + C++ ownership + validation plan.
2. Inspect the existing Blueprint classes, components, widgets, structs, enums, and data assets involved.
3. Lock the interface shape before writing reflected declarations.
4. Implement the minimum C++ behind that shape.
5. Compile and validate the smallest useful loop.
6. Expand behavior only after the first slice works.
