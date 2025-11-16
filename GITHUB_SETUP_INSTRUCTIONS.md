# Creating TinyUSB Fork as Separate GitHub Repository

## Current Status

✅ Repository prepared with:
- Your NCM fix commit (6be33fa67)
- RW612 BSP additions commit (8b2c86d36)
- Upstream remote configured as 'upstream'

## Steps to Create GitHub Repository

### Option 1: Using GitHub Web Interface (Recommended)

1. **Go to GitHub:**
   - Navigate to https://github.com/new
   - Or click the "+" icon → "New repository"

2. **Repository Settings:**
   ```
   Repository name: tinyusb-rw612
   Description: TinyUSB with RW612 NCM support for Windows 10+
   Visibility: Public (or Private if you prefer)
   
   ❌ DO NOT initialize with:
      - README
      - .gitignore
      - License
   (The repository already has these)
   ```

3. **Create Repository**
   - Click "Create repository"
   - You'll see a page with setup instructions
   - Copy your repository URL (should be like: `https://github.com/YOUR_USERNAME/tinyusb-rw612.git`)

### Option 2: Using GitHub CLI (If Installed)

```bash
cd /Users/denissuprunenko/repos/frdmrw612_freertos_hello/app_libs/tinyusb

# Create the repository
gh repo create tinyusb-rw612 \
  --public \
  --description "TinyUSB with RW612 NCM support for Windows 10+" \
  --source=. \
  --remote=origin \
  --push
```

---

## Manual Setup (After Creating GitHub Repo)

### 1. Add Your GitHub Repository as Origin

```bash
cd /Users/denissuprunenko/repos/frdmrw612_freertos_hello/app_libs/tinyusb

# Add your GitHub repo as 'origin' (replace YOUR_USERNAME)
git remote add origin https://github.com/YOUR_USERNAME/tinyusb-rw612.git

# Verify remotes
git remote -v
# Should show:
#   origin    https://github.com/YOUR_USERNAME/tinyusb-rw612.git (fetch)
#   origin    https://github.com/YOUR_USERNAME/tinyusb-rw612.git (push)
#   upstream  https://github.com/hathach/tinyusb.git (fetch)
#   upstream  https://github.com/hathach/tinyusb.git (push)
```

### 2. Push Your Changes to GitHub

```bash
# Push master branch to your new repository
git push -u origin master

# Push all tags (if any)
git push origin --tags
```

### 3. Verify on GitHub

Go to: `https://github.com/YOUR_USERNAME/tinyusb-rw612`

You should see:
- Your 2 commits ahead of TinyUSB upstream
- The NCM fix in the commit history
- All TinyUSB source files

---

## Repository Structure After Setup

```
Local TinyUSB Repository
├── .git/
│   └── config
│       ├── [remote "origin"]     → Your GitHub fork
│       └── [remote "upstream"]   → Official TinyUSB
├── Your commits:
│   ├── 6be33fa67 - NCM fix for RW612
│   └── 8b2c86d36 - RW612 BSP additions
└── Upstream commits:
    └── 20c364422 - Latest TinyUSB master
```

---

## Recommended GitHub Repository Settings

### Repository Details
- **Name:** `tinyusb-rw612`
- **Description:** "TinyUSB fork with RW612 NCM support for Windows 10+ and FRDM-RW612 board improvements"
- **Topics:** Add tags:
  - `tinyusb`
  - `usb`
  - `rw612`
  - `ncm`
  - `embedded`
  - `nxp`
  - `arm`

### README Addition
Consider adding a section to the README:

```markdown
## RW612 Enhancements

This fork includes improvements for the NXP FRDM-RW612 board:

### NCM Support for Windows 10+ (Instead of RNDIS)
- **Issue:** RNDIS example didn't work on Windows PC
- **Fix:** Enabled NCM (Network Control Model) protocol
- **Benefit:** Auto-driver installation on Windows 10+
- **File:** `examples/device/net_lwip_webserver/src/tusb_config.h`
- **Commit:** 6be33fa67

### Features
- ✅ Built-in Windows 10+ driver (no manual installation)
- ✅ Better performance with packet aggregation
- ✅ Standard USB CDC protocol
- ✅ 55KB binary size (optimized)

### Testing
```bash
cd examples/device/net_lwip_webserver
make BOARD=frdm_rw612
make BOARD=frdm_rw612 flash-jlink
```

Connect to Windows PC:
- Device IP: 192.168.7.1
- PC IP: 192.168.7.2 (auto-assigned)
- Web server: http://192.168.7.1
```

---

## Syncing with Upstream TinyUSB

To keep your fork updated with official TinyUSB:

```bash
# Fetch latest from upstream
git fetch upstream

# Merge upstream changes into your master
git checkout master
git merge upstream/master

# Resolve any conflicts if needed
# Then push to your fork
git push origin master
```

---

## Alternative: Contributing Back to TinyUSB

If you want to contribute your NCM fix to the official TinyUSB:

1. **Fork TinyUSB on GitHub** (through web interface)
2. **Push your branch:**
   ```bash
   git checkout -b fix-rw612-ncm
   git push origin fix-rw612-ncm
   ```
3. **Create Pull Request:**
   - Go to your fork on GitHub
   - Click "Pull Request"
   - Target: `hathach/tinyusb:master`
   - Source: `YOUR_USERNAME/tinyusb-rw612:fix-rw612-ncm`
   - Title: "fix(rw612): Enable NCM for Windows 10+ compatibility"
   - Description: Include your commit message details

---

## Quick Reference

### Repository URLs
- **Upstream (Official):** https://github.com/hathach/tinyusb
- **Your Fork:** https://github.com/YOUR_USERNAME/tinyusb-rw612 (after creation)

### Common Commands
```bash
# Check status
git status

# View remotes
git remote -v

# Push changes
git push origin master

# Pull upstream updates
git pull upstream master

# View your commits
git log --oneline -5
```

---

## Notes

- ✅ Repository is ready to push
- ✅ Upstream remote preserved for future updates
- ✅ Your NCM fix is committed and ready
- ⏳ Waiting for you to create GitHub repository
- ⏳ Waiting for origin remote configuration
- ⏳ Waiting for initial push

**Next Step:** Create the GitHub repository using Option 1 or 2 above, then run the push command.

