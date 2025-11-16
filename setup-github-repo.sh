#!/bin/bash
# Setup script for creating TinyUSB fork on GitHub
# This script helps you configure the GitHub remote after creating the repository

set -e  # Exit on error

REPO_DIR="/Users/denissuprunenko/repos/frdmrw612_freertos_hello/app_libs/tinyusb"
cd "$REPO_DIR"

echo "======================================"
echo "TinyUSB GitHub Repository Setup"
echo "======================================"
echo ""

# Check if repository is ready
if ! git rev-parse --git-dir > /dev/null 2>&1; then
    echo "❌ Error: Not a git repository"
    exit 1
fi

echo "✅ Git repository found"
echo ""

# Check current remotes
echo "Current remotes:"
git remote -v
echo ""

# Check if origin already exists
if git remote | grep -q "^origin$"; then
    echo "⚠️  Warning: 'origin' remote already exists"
    echo "Current origin: $(git remote get-url origin)"
    read -p "Do you want to replace it? (y/N): " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        git remote remove origin
        echo "✅ Removed existing origin"
    else
        echo "❌ Cancelled. Please remove origin manually: git remote remove origin"
        exit 1
    fi
fi

# Prompt for GitHub username
echo ""
echo "Please enter your GitHub username:"
read -r GITHUB_USERNAME

if [ -z "$GITHUB_USERNAME" ]; then
    echo "❌ Error: GitHub username cannot be empty"
    exit 1
fi

# Suggest repository name
REPO_NAME="tinyusb-rw612"
echo ""
echo "Suggested repository name: $REPO_NAME"
read -p "Press Enter to use this name, or type a different name: " CUSTOM_NAME

if [ -n "$CUSTOM_NAME" ]; then
    REPO_NAME="$CUSTOM_NAME"
fi

# Construct GitHub URL
GITHUB_URL="https://github.com/$GITHUB_USERNAME/$REPO_NAME.git"

echo ""
echo "======================================"
echo "Repository Configuration"
echo "======================================"
echo "GitHub Username: $GITHUB_USERNAME"
echo "Repository Name: $REPO_NAME"
echo "Repository URL:  $GITHUB_URL"
echo ""

# Instructions to create GitHub repo
echo "======================================"
echo "STEP 1: Create GitHub Repository"
echo "======================================"
echo ""
echo "Please go to: https://github.com/new"
echo ""
echo "Settings to use:"
echo "  Repository name: $REPO_NAME"
echo "  Description:     TinyUSB with RW612 NCM support for Windows 10+"
echo "  Visibility:      Public (or Private)"
echo "  ❌ DO NOT check: Initialize with README, .gitignore, or License"
echo ""
read -p "Press Enter after you've created the repository on GitHub..."

# Add the new remote
echo ""
echo "======================================"
echo "STEP 2: Adding GitHub Remote"
echo "======================================"
echo ""
echo "Adding '$GITHUB_URL' as origin..."

if git remote add origin "$GITHUB_URL"; then
    echo "✅ Remote 'origin' added successfully"
else
    echo "❌ Failed to add remote"
    exit 1
fi

echo ""
echo "Current remotes:"
git remote -v
echo ""

# Ask about pushing
echo "======================================"
echo "STEP 3: Push to GitHub"
echo "======================================"
echo ""
read -p "Do you want to push your commits now? (Y/n): " -n 1 -r
echo ""

if [[ ! $REPLY =~ ^[Nn]$ ]]; then
    echo ""
    echo "Pushing to GitHub..."
    echo ""
    
    if git push -u origin master; then
        echo ""
        echo "✅ Successfully pushed to GitHub!"
        echo ""
        echo "Your repository is now available at:"
        echo "https://github.com/$GITHUB_USERNAME/$REPO_NAME"
    else
        echo ""
        echo "❌ Push failed. Common reasons:"
        echo "   - Repository doesn't exist on GitHub"
        echo "   - Authentication failed (check SSH keys or HTTPS credentials)"
        echo "   - Network connection issue"
        echo ""
        echo "You can try pushing manually later with:"
        echo "   git push -u origin master"
    fi
else
    echo ""
    echo "Skipped push. You can push manually later with:"
    echo "   cd $REPO_DIR"
    echo "   git push -u origin master"
fi

echo ""
echo "======================================"
echo "Setup Complete!"
echo "======================================"
echo ""
echo "Repository structure:"
echo "  upstream → https://github.com/hathach/tinyusb.git (official)"
echo "  origin   → $GITHUB_URL (your fork)"
echo ""
echo "Your commits ready to push:"
git log --oneline -2
echo ""
echo "Next steps:"
echo "  1. Visit: https://github.com/$GITHUB_USERNAME/$REPO_NAME"
echo "  2. Add topics: tinyusb, usb, rw612, ncm, embedded"
echo "  3. Update README with RW612 enhancements"
echo ""
echo "See GITHUB_SETUP_INSTRUCTIONS.md for more details"

