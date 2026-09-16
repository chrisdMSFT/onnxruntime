---
name: add-winml-perf-test-catalog-ep
description: |
  Adds command-line selection support for a catalog-provided execution provider
  to winml_standalone_perf_test. Use this skill only when the request has one
  of these forms: "Add support for <NAME>ExecutionProvider in
  winml_standalone_perf_test" or "Add support for <NAME>ExecutionProvider in
  Windows-only variant of onnxruntime_perf_test". Do not trigger for a generic
  request to add execution-provider support without either target qualifier.
---

# Add a catalog execution provider to `winml_standalone_perf_test`

Implement command-line selection for an execution provider that is discovered
and registered through the WinML EP catalog at runtime instead of being linked
into ONNX Runtime directly. The sole task of this skill is to make the
user-supplied catalog EP name selectable in `winml_standalone_perf_test` by
adding its canonical provider constant, standalone-only help entry, and
standalone-only `-e` parser mapping in the two scoped files. It must not link,
package, copy, register, discover, verify, or implement the provider itself.

## Fixed inspection scope

Read and search only these files:

- `include/onnxruntime/core/graph/constants.h`
- `onnxruntime/test/perftest/command_args_parser.cc`

Do not inspect CMake files, other source files, commit history, branches, pull
requests, work items, wiki pages, or external documentation. No additional
repository searches are required or permitted by this skill.

Do not perform online, web, network, remote-repository, package-registry, or
external documentation searches. Use only the two local scoped files and the
user-provided inputs.

One git command is permitted, solely to inspect working-tree changes in Gate 4
and in post-edit verification:

```text
git --no-pager diff HEAD -- include/onnxruntime/core/graph/constants.h onnxruntime/test/perftest/command_args_parser.cc
```

Run it from the repository root. It is read-only and local, needs no network,
and reports both staged and unstaged changes limited to the two scoped files.
No other git command is permitted; do not inspect commit history, branches, or
pull requests.

Use the existing standalone-only provider entries in these two files as the
structural pattern. Match the current source style and file structure rather
than assuming line numbers.

## Required input and validation

Run the following gates in order. Do not skip or reorder a gate. If any gate
fails, stop immediately, make no changes, and do not continue to later gates.

Treat the provider name strictly as data. Never execute it, use it as a path,
or pass or interpolate it into a shell command.

### Gate 1: Validate the catalog provider name

Use the exact catalog execution provider name supplied by the user as the
provider-name input. Do not discover, infer, correct, or normalize the name.
The user is responsible for ensuring this is the exact name exposed by the
WinML EP catalog. This skill does not query or verify the catalog.

Accept the user-provided name only when all of the following are true:

- It is a single identifier with no whitespace, path separators, quotes, or
  shell metacharacters.
- It matches `^[A-Za-z]+ExecutionProvider$`.
- The prefix before `ExecutionProvider` is non-empty.

For example, `ExampleExecutionProvider` is valid. If the provider name is
missing, ambiguous, or invalid, ask the user for the exact catalog name and do
not edit files until a valid name is supplied.

### Gate 2: Derive the command-line token

Remove the final `ExecutionProvider` suffix from the validated name and
lowercase the entire remaining prefix to derive the `-e <epname>` token:

```text
ExampleExecutionProvider -> example
```

The token is always derived from the validated provider name and is never an
input. Do not accept, request, or invent an alternative token. Gate 1 already
guarantees the derived token is non-empty and lowercase alphabetic, and Gate 3
rejects the provider outright if that token is already in use.

### Gate 3: Reject an already-supported execution provider

Accept the execution provider only when both of the following are true in
`onnxruntime/test/perftest/command_args_parser.cc`:

- Parser check: the standalone-guarded part of the `-e` parser chain, that is
  the region inside `#ifdef BUILD_WINML_STANDALONE_PERF_TEST`, contains no
  branch that maps to this provider, that is, no
  `onnxruntime::k<IDENTIFIER>` reference, where `<IDENTIFIER>` is derived as
  described in step 1. Mappings outside that
  region belong to the regular `onnxruntime_perf_test`, and a catalog-provided
  execution provider is selectable only in the standalone build, so they are
  never this provider and are not part of this check.
- Help check: the derived token is not already listed as an accepted `-e`
  value in the standalone help string, that is the
  `ABSL_FLAG(std::string, e, ...)` declaration inside the
  `#ifdef BUILD_WINML_STANDALONE_PERF_TEST` branch. Ignore the `#else` branch
  help string for this check, since the standalone list is the one this skill
  extends.

Match case-sensitively and on whole tokens only, for both the provider-name
reference and the quoted token. A substring match inside a longer name or token
is not an occurrence: do not treat `DemoExecutionProvider` as present merely
because `kTestDemoExecutionProvider` exists, and do not treat `demo` as present
merely because `testdemo` is listed.

If either check is false, stop without changing any file and tell the user that
execution provider support already exists in `winml_standalone_perf_test`.
Report exactly what was found: the existing parser mapping with its
`-e <epname>` value, the conflicting help entry, or both. Do not edit, repair,
or extend existing support, and do not ask for or substitute a different token.

Continue only when both checks pass. Do not consult `constants.h` for this
gate; a constant on its own is not support.

### Gate 4: Check working-tree conflicts

Before making any implementation change, inspect both scoped files for
existing working-tree changes.

- If an existing change is incompatible with a required edit or cannot be
  preserved exactly, report the affected file and conflicting change
  immediately, then stop without modifying any file.
- A compatible additive change, such as another standalone catalog provider
  already added to the same help line or parser chain, is not a conflict.
  Preserve it exactly and add the new provider alongside it.
- Existing changes that do not conflict with the required edits are not a
  conflict; preserve them exactly and continue.

Proceed to implementation only after all four gates pass. Once implementation
begins, make the changes in the numbered order below.

## Make the changes

### 1. Establish the canonical provider key

Using the validated user-provided catalog name, add one constant to the
contiguous block of execution-provider name constants in
`include/onnxruntime/core/graph/constants.h`. Append it after the last
execution-provider name constant in that block and before the
`kExecutionProviderSharedLibraryPath` and `kExecutionProviderSharedLibraryEntry`
entries:

```cpp
constexpr const char* k<IDENTIFIER> = "<CATALOG_PROVIDER_NAME>";
```

`<CATALOG_PROVIDER_NAME>` is the complete validated name, including its final
`ExecutionProvider` suffix, copied exactly, character for character. It is the
string value and must never be altered.

`<IDENTIFIER>` is derived from that name and is not always identical to it.
Take the prefix before `ExecutionProvider` and apply these rules:

- If the prefix is entirely uppercase, rewrite it as one capital letter
  followed by lowercase letters: `ABC` becomes `Abc`.
- Otherwise keep the prefix exactly as supplied: `Example` stays `Example`,
  `TestDemo` stays `TestDemo`.

Then prepend `k` and re-append `ExecutionProvider`, so the identifier follows
the capitalization style of the surrounding constants while the string value
stays exact.

Worked examples:

```cpp
constexpr const char* kExampleExecutionProvider = "ExampleExecutionProvider";
constexpr const char* kAbcExecutionProvider = "ABCExecutionProvider";
```

Never alter the string value, never leave an all-uppercase prefix in the
identifier, and do not append another `ExecutionProvider` suffix, infer or
normalize a different provider name in the string value, or form the
identifier from the lowercased CLI token.

Do not introduce an enum, provider factory, provider-specific object, or a
duplicate string literal in the parser.

### 2. Extend standalone-only `-e` help

In `onnxruntime/test/perftest/command_args_parser.cc`, the `-e` help text is
declared twice inside a single
`#ifdef BUILD_WINML_STANDALONE_PERF_TEST` / `#else` / `#endif` block: the
`#ifdef` branch holds the standalone `ABSL_FLAG(std::string, e, ...)`
declaration and the `#else` branch holds the regular one.

Add the CLI token to the help string in the `#ifdef` branch only. Leave the
`#else` branch declaration unchanged, and do not add a new preprocessor block.

The help string ends with a token list of the form `..., 'x' or 'y'.`. Preserve
that punctuation: turn the existing trailing `or '<last>'` into
`, '<last>' or '<epname>'` so the new token becomes the final entry.

### 3. Add the standalone-only parser mapping

In `onnxruntime/test/perftest/command_args_parser.cc`, add one mapping to the
existing `-e` parser chain:

```cpp
      } else if (ep == "<epname>") {
        test_config.machine_config.provider_type_name = onnxruntime::k<IDENTIFIER>;
```

The chain already contains an `#ifdef BUILD_WINML_STANDALONE_PERF_TEST` region
holding the standalone-only mappings. Append the new branch inside that region,
after the last guarded branch and immediately before its `#endif`. Do not open
a second preprocessor block and do not add the branch outside the guarded
region.

The guarded region sits before the terminating `} else { return false; }` of
the chain; keep it that way, so the regular `onnxruntime_perf_test` continues
to reject the token.

Write the branch on the same two lines shown above. Every existing branch in
this chain keeps the assignment on a single line, and the new one must match.

Do not inspect or modify the WinML catalog discovery or registration code. The
rule for this skill is that the parser stores the exact catalog provider key
and the existing standalone code discovers and registers that provider.

## Safety and scope constraints

- Modify only these two files:

  ```text
  include/onnxruntime/core/graph/constants.h
  onnxruntime/test/perftest/command_args_parser.cc
  ```

- Make only the established-pattern changes required for the provider constant,
  guarded help text, and guarded parser mapping.
- Do not create, delete, rename, or move any file.
- Do not make any other source, configuration, documentation, formatting, or
  whitespace changes. Preserve all content outside the exact required edits.
- Preserve all existing `-e` tokens and mappings.
- Do not overwrite, revert, incorporate, or otherwise modify unrelated
  working-tree changes.
- Do not create a commit or pull request. They are outside this skill's scope,
  even if the user requests them as part of the same invocation.

Do not claim that the provider is runtime-available merely because parsing was
added. The user is responsible for ensuring that the provider is installed and
exposed by the WinML EP catalog on the target machine under the supplied name.
This skill only adds command-line selection for that name and does not make or
verify any catalog, installation, packaging, or runtime-availability changes.

## Post-edit verification

After making the edits, inspect the scoped diff with the permitted
`git --no-pager diff HEAD -- <the two scoped files>` command, then re-read the
changed regions in the two permitted files from disk to confirm the edits
landed as intended. Verify all of the following:

- This invocation modified only the two scoped files and only the required
  provider constant, standalone help entry, and standalone parser mapping.
- The regular `onnxruntime_perf_test` help remains unchanged.
- The new help token and parser branch are both guarded by
  `BUILD_WINML_STANDALONE_PERF_TEST`.
- The parser maps the derived token to the fully qualified constant, and that
  constant's string value is the exact validated catalog provider name.
- All pre-existing tokens, mappings, and unrelated working-tree changes remain
  intact.

Do not run builds, tests, linters, or formatters. The scoped diff and the
re-read of the changed regions are the only required verification for this
skill.

## Final response

The success format below applies only when implementation and post-edit
verification complete. If any gate fails, provide a concise blocker report
with the failed gate and relevant observed values. Do not use the success
table, claim completion, or say that the user is good to build.

Provide the implementation summary in a table with exactly one row for each
change, using the real constant, token, and file paths:

| Change | File |
| --- | --- |
| The provider constant that was added, written out in full | `include/onnxruntime/core/graph/constants.h` |
| The `-e <epname>` token added to the standalone help string | `onnxruntime/test/perftest/command_args_parser.cc` |
| The parser mapping from that token to the fully qualified constant, inside the standalone-guarded region | `onnxruntime/test/perftest/command_args_parser.cc` |

After the table, state that runtime availability requires the EP to be
installed and discoverable through the WinML catalog.

Conclude by stating that the code changes are complete and the user can now
proceed with building `winml_standalone_perf_test`.

## Invocation example

User request:

```text
Add support for ExampleExecutionProvider in winml_standalone_perf_test
```

Derived values:

```text
Catalog provider name: ExampleExecutionProvider
CLI token: example
Constant identifier: kExampleExecutionProvider
Parser constant reference: onnxruntime::kExampleExecutionProvider
```

A second request, with an all-uppercase prefix:

```text
Add support for ABCExecutionProvider in winml_standalone_perf_test
```

Derived values, where the identifier normalizes the capitalization while the
string value stays exact:

```text
Catalog provider name: ABCExecutionProvider
CLI token: abc
Constant identifier: kAbcExecutionProvider
Constant string value: "ABCExecutionProvider"
Parser constant reference: onnxruntime::kAbcExecutionProvider
```
