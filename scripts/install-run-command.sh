#!/usr/bin/env bash
set -eu

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_DIR="$HOME/.local/bin"
RUN_FILE="$BIN_DIR/run"

mkdir -p "$BIN_DIR"

cat > "$RUN_FILE" <<EOF
#!/usr/bin/env bash
set -eu

case "\${1:-}" in
    chess)
        shift
        exec "$PROJECT_ROOT/scripts/run-chess.sh" "\$@"
        ;;
    Xadrez)
        echo "Use: run chess"
        exit 2
        ;;
    *)
        echo "Usage: run chess [build|clean-build|flash|monitor] | run map"
        exit 2
        ;;
esac
EOF

chmod +x "$RUN_FILE"
chmod +x "$PROJECT_ROOT/scripts/run-chess.sh"
chmod +x "$PROJECT_ROOT/scripts/run-map.sh"

case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *)
        if [ -f "$HOME/.bashrc" ]; then
            grep -qxF 'export PATH="$HOME/.local/bin:$PATH"' "$HOME/.bashrc" || printf "%s\n" 'export PATH="$HOME/.local/bin:$PATH"' >> "$HOME/.bashrc"
        fi
        ;;
esac

echo "Installed: $RUN_FILE"
echo "Restart the terminal or run:"
echo "export PATH=\"\$HOME/.local/bin:\$PATH\""
echo
echo "Then use:"
echo "run chess"
echo "run map"
