#!/bin/bash
# Backup script for systems-craft repository

BACKUP_DIR="$HOME/github-backups/systems-craft"
DATE=$(date +%Y-%m-%d-%H%M%S)

echo "🔄 Starting backup at $DATE..."

# Create backup directory
mkdir -p "$BACKUP_DIR"

# Clone/update mirror backup
if [ -d "$BACKUP_DIR/mirror/.git" ]; then
    echo "📥 Updating existing backup..."
    cd "$BACKUP_DIR/mirror"
    git fetch --all --prune
else
    echo "📦 Creating initial mirror clone..."
    git clone --mirror https://github.com/kapil0x/systems-craft.git "$BACKUP_DIR/mirror"
fi

# Create timestamped archive
echo "🗜️  Creating compressed archive..."
cd "$BACKUP_DIR"
tar -czf "systems-craft-${DATE}.tar.gz" mirror/

# Keep only last 10 backups
echo "🧹 Cleaning old backups..."
ls -t systems-craft-*.tar.gz | tail -n +11 | xargs -r rm

echo "✅ Backup completed successfully!"
echo "📁 Location: $BACKUP_DIR/systems-craft-${DATE}.tar.gz"
echo "💾 Total backups: $(ls -1 systems-craft-*.tar.gz 2>/dev/null | wc -l)"
