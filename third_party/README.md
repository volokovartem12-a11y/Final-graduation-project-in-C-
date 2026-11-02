# Third-party dependencies

Dependencies are included so configuration and builds work without network access.

| Dependency | Version | File | SHA-256 | License |
| --- | --- | --- | --- | --- |
| nlohmann/json | 3.11.3 | `nlohmann/json.hpp` | `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6` | MIT, `nlohmann/LICENSE.MIT` |
| GoogleTest | 1.14.0 | `googletest-v1.14.0.zip` | `1f357c27ca988c3f7c6b4bf68a9395005ac6761f034046e9dde0896e3aba00e4` | BSD-3-Clause, `LICENSE` inside archive |

Official sources:

- https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp
- https://codeload.github.com/google/googletest/zip/refs/tags/v1.14.0

Only the test target links GoogleTest; the application links the search core.
