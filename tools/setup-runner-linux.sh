#!/bin/sh
# Make this Linux machine jouster's build machine. Run it once:
#
#     ./tools/setup-runner-linux.sh
#
# After that, every push to jouster -- or "Run workflow" on the build workflow
# in GitHub's Actions tab -- builds the latest code HERE, where the dump is,
# and puts Jouster.nro on the Switch over USB if it is sitting in hbmenu.
#
# Why here and not on GitHub's servers: the build needs your tfbGame_cafe.rpx,
# which must never be uploaded, and a built Jouster.nro is game output that
# LICENSE section 6 says must not be redistributed -- so it is never uploaded
# as an artifact either. GitHub only says "build now"; this machine does it.
#
# What it sets up:
#   - the GitHub Actions runner, in ~/.local/share/arkchemy-runner, labelled
#     self-hosted/linux/arkchemy
#   - a systemd --user service, so it runs whenever you are logged in; no
#     root, no system packages (it works on Bluefin/Silverblue as is)
#   - the path to this checkout, which the workflow builds in
#
# Everything else -- recomp, devkitPro, the portlibs -- lives in podman
# images that tools/linux-all.sh builds on first use.
#
# To remove it:
#   systemctl --user disable --now arkchemy-runner
#   ~/.local/share/arkchemy-runner/config.sh remove --token <token from the same page>
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO="Arkchemy/jouster"
DIR="${ARK_RUNNER_DIR:-$HOME/.local/share/arkchemy-runner}"
UNIT="$HOME/.config/systemd/user/arkchemy-runner.service"

say()  { printf '\n\033[36m==> %s\033[0m\n' "$1"; }
ok()   { printf '\033[32mok  %s\033[0m\n' "$1"; }
die()  { printf '\033[31mxx  %s\033[0m\n' "$1"; exit 1; }

command -v podman >/dev/null || die "podman not found"
command -v systemctl >/dev/null || die "systemd not found"

# ------------------------------------------------------------- the runner
if [ ! -x "$DIR/run.sh" ]; then
    say "downloading the GitHub Actions runner"
    ver="$(curl -fsSL https://api.github.com/repos/actions/runner/releases/latest | sed -n 's/.*"tag_name": *"v\([^"]*\)".*/\1/p' | head -1)"
    [ -n "$ver" ] || die "could not find the latest runner version"
    mkdir -p "$DIR"
    curl -fsSL "https://github.com/actions/runner/releases/download/v$ver/actions-runner-linux-x64-$ver.tar.gz" | tar -xz -C "$DIR"
    ok "runner $ver in $DIR"
fi

if [ ! -f "$DIR/.runner" ]; then
    say "registering it with $REPO"
    token="${ARK_RUNNER_TOKEN:-}"
    # With the GitHub CLI logged in as a repository admin, no copying at all.
    if [ -z "$token" ] && command -v gh >/dev/null && gh auth status >/dev/null 2>&1; then
        token="$(gh api -X POST "repos/$REPO/actions/runners/registration-token" --jq .token 2>/dev/null || true)"
        [ -n "$token" ] && ok "got a registration token from gh"
    fi
    if [ -z "$token" ]; then
        # The settings page answers 404, not "forbidden", to anyone who is not
        # an admin of the repository -- including when the browser is logged
        # in as a different account.
        page="https://github.com/$REPO/settings/actions/runners/new?arch=x64&os=linux"
        printf 'Open (logged in as the account that owns %s): %s\n' "${REPO%%/*}" "$page"
        printf 'It is Settings -> Actions -> Runners -> New self-hosted runner on the repository.\n'
        printf 'A 404 there means that browser is not logged in as a repository admin.\n'
        printf 'Or: gh auth login, then run this script again and it fetches the token itself.\n'
        (xdg-open "$page" >/dev/null 2>&1 &) || true
        printf 'Paste the token from the "./config.sh --url ... --token XXXX" line: '
        read -r token
    fi
    [ -n "$token" ] || die "no token"
    (cd "$DIR" && ./config.sh --unattended --replace --url "https://github.com/$REPO" --token "$token" \
        --name "$(hostname)-linux" --labels arkchemy,linux --work _work)
    ok "registered"
fi

# The workflow builds in this checkout (it holds the generated C and the
# incremental build), and the runner reads this file into every job's
# environment. The .NET runtime inside the runner wants libicu, which an
# immutable image may not carry and cannot easily gain; invariant mode
# removes the need.
{
    printf 'ARK_REPO=%s\n' "$ROOT"
    printf 'DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1\n'
} > "$DIR/.env"
ok "builds will run in $ROOT"

# ------------------------------------------------------------- the service
mkdir -p "$(dirname "$UNIT")"
cat > "$UNIT" <<UNITEOF
[Unit]
Description=Arkchemy jouster build runner (GitHub Actions, self-hosted)
After=network-online.target

[Service]
WorkingDirectory=$DIR
ExecStart=$DIR/run.sh
Restart=always
RestartSec=15

[Install]
WantedBy=default.target
UNITEOF
systemctl --user daemon-reload
systemctl --user enable --now arkchemy-runner
ok "running as a user service (systemctl --user status arkchemy-runner)"

# ------------------------------------------------------------- the dump
# Found and remembered now, so the first push does not have to go looking.
if [ ! -s "${XDG_CONFIG_HOME:-$HOME/.config}/arkchemy/rpx" ]; then
    say "looking for tfbGame_cafe.rpx under $HOME"
    rpx="$(find "$HOME" -xdev -maxdepth 7 -name tfbGame_cafe.rpx -print 2>/dev/null | head -1 || true)"
    if [ -z "$rpx" ]; then
        printf 'Not found. Path to your tfbGame_cafe.rpx: '
        read -r rpx
    fi
    [ -f "$rpx" ] || die "no such file: $rpx"
    mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/arkchemy"
    printf '%s\n' "$(cd "$(dirname "$rpx")" && pwd)/$(basename "$rpx")" > "${XDG_CONFIG_HOME:-$HOME/.config}/arkchemy/rpx"
fi
ok "dump: $(cat "${XDG_CONFIG_HOME:-$HOME/.config}/arkchemy/rpx")"

say "done -- push to jouster, or press Run workflow on https://github.com/$REPO/actions/workflows/build.yml"
