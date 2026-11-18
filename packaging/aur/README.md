# Arch Linux / AUR Packaging for OnDeck-Juice

This directory contains `PKGBUILD` files for building OnDeck-Juice on Arch Linux or submitting to the Arch User Repository (AUR).

## Installing locally on Arch Linux

1. Clone or copy this directory:
   ```bash
   cd packaging/aur
   ```
2. Build and install with `makepkg`:
   ```bash
   makepkg -si
   ```

## Publishing to AUR

1. Create or claim the repository on AUR (`ssh://aur@aur.archlinux.org/ondeck-juice.git` or `ondeck-juice-git.git`).
2. Generate `.SRCINFO`:
   ```bash
   makepkg --printsrcinfo > .SRCINFO
   ```
3. Commit both `PKGBUILD` and `.SRCINFO` to the AUR git repository:
   ```bash
   git add PKGBUILD .SRCINFO
   git commit -m "Initial release"
   git push origin master
   ```

Once published, Arch users can install directly using any AUR helper:
```bash
yay -S ondeck-juice
# or
paru -S ondeck-juice
```
