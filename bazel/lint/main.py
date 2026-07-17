"""Command-line entry point for APGAR's lint runner."""

from bazel.lint import lint

if __name__ == "__main__":
    raise SystemExit(lint.main())
