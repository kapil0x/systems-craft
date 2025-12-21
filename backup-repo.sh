#!/bin/bash
# Backup script for systems-craft repository

BACKUP_DIR="$HOME/github-backups/systems-craft"
LOG_FILE="$HOME/backup.log"
DATE=$(date +%Y-%m-%d-%H%M%S)
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

# Function to log with timestamp
log() {
    echo "[$TIMESTAMP] $1" | tee -a "$LOG_FILE"
}

# Function to send notification
notify() {
    local title="$1"
    local message="$2"
    osascript -e "display notification \"$message\" with title \"$title\"" 2>/dev/null
}

log "🔄 Starting backup at $DATE..."

# Create backup directory
mkdir -p "$BACKUP_DIR"

# Clone/update mirror backup
if [ -d "$BACKUP_DIR/mirror/.git" ]; then
    log "📥 Updating existing backup..."
    cd "$BACKUP_DIR/mirror"
    if git fetch --all --prune 2>&1 | tee -a "$LOG_FILE"; then
        log "✓ Git fetch completed successfully"
    else
        log "✗ ERROR: Git fetch failed"
        notify "Backup Failed" "Git fetch failed for systems-craft"
        exit 1
    fi
else
    log "📦 Creating initial mirror clone..."
    if git clone --mirror https://github.com/kapil0x/systems-craft.git "$BACKUP_DIR/mirror" 2>&1 | tee -a "$LOG_FILE"; then
        log "✓ Initial clone completed successfully"
    else
        log "✗ ERROR: Git clone failed"
        notify "Backup Failed" "Initial clone failed for systems-craft"
        exit 1
    fi
fi

# Create timestamped archive
log "🗜️  Creating compressed archive..."
cd "$BACKUP_DIR"
if tar -czf "systems-craft-${DATE}.tar.gz" mirror/ 2>&1 | tee -a "$LOG_FILE"; then
    ARCHIVE_SIZE=$(du -h "systems-craft-${DATE}.tar.gz" | cut -f1)
    log "✓ Archive created: systems-craft-${DATE}.tar.gz ($ARCHIVE_SIZE)"
else
    log "✗ ERROR: Archive creation failed"
    notify "Backup Failed" "Archive creation failed for systems-craft"
    exit 1
fi

# Keep only last 10 backups
log "🧹 Cleaning old backups..."
REMOVED=$(ls -t systems-craft-*.tar.gz 2>/dev/null | tail -n +11)
if [ -n "$REMOVED" ]; then
    echo "$REMOVED" | xargs rm
    log "✓ Removed old backups: $(echo "$REMOVED" | wc -l | tr -d ' ') files"
else
    log "✓ No old backups to remove"
fi

TOTAL_BACKUPS=$(ls -1 systems-craft-*.tar.gz 2>/dev/null | wc -l | tr -d ' ')
log "✅ Backup completed successfully!"
log "📁 Location: $BACKUP_DIR/systems-craft-${DATE}.tar.gz"
log "💾 Total backups: $TOTAL_BACKUPS"
log "----------------------------------------"

# Send success notification
notify "Backup Successful" "systems-craft backup completed ($ARCHIVE_SIZE, $TOTAL_BACKUPS total backups)"
