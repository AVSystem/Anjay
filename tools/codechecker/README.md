# CodeChecker

This directory contains separate CodeChecker configurations for the standard
analyzer set (~2 mins on a PC) and cppcheck-only (~15 mins on a PC) analysis. Each analyzer's exclusions are kept in
its analysis directory, while suppressions are shared in `suppressions.yaml`.

## Prerequisites and installation

Install the system packages required by the configured analyzers. Clang is pinned to v20 to make the analysis reproducible:

```sh
sudo apt install clang-20 clang-tools-20 clang-tidy-20
```

`CodeChecker==6.28.0` is listed in the repository's `requirements.txt`; it is
installed when setting up the development environment with `./devconfig`.

Install Meta's Infer separately, following the
[Infer getting-started guide](https://fbinfer.com/docs/getting-started/).

cppcheck needs to be manually compiled in version 2.21.0, following the
[README](https://github.com/cppcheck-opensource/cppcheck)

## Running the analysis
```sh
./devconfig -DWITH_STATIC_ANALYSIS=ON -DWITH_NESTED_FUNCTION_MUTEX_LOCKS=OFF -DWITH_TEST=OFF
make analyze
make cppcheck_analyze
```

For more information look in `tools/codechecker/static_analyze.py`

## VSCode extension setup

### Global settings
```json
{
    "codechecker.executor.analysisTimeout": 0,
    "codechecker.analyze.runOnSave": false,
    "codechecker.analyze.threadCount": 8,
    "codechecker.executor.enableNotifications": false,
}
```

### Workspace setttings
```json
{
    "codechecker.backend.compilationDatabasePath": "${workspaceFolder}/compile_commands.json",
    "codechecker.backend.outputFolder": "${workspaceFolder}/tools/codechecker/standard_analysis",
    "codechecker.analyze.arguments": "--config tools/codechecker/standard_analysis/config.json --output tools/codechecker/standard_analysis/reports",
}
```

### Task to analyze the current file

Add this to `.vscode/tasks.json`:

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "CodeChecker: Analyze current file",
            "type": "shell",
            "command": "python3",
            "args": [
                "${workspaceFolder}/tools/codechecker/static_analyze.py",
                "analyze_file",
                "${file}"
            ],
            "options": {
                "cwd": "${workspaceFolder}"
            },
            "problemMatcher": []
        }
    ]
}
```
