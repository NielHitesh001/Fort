import sys
from .cli import main

if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, OSError) as exc:
        print(f"Fort backtest: {exc}", file=sys.stderr)
        raise SystemExit(1)
