# Systems Craft Progress Tracking - Testing Guide

This guide walks you through testing the complete enrollment and progress tracking system.

## 📋 Prerequisites

- A local Systems Craft server running (see README.md for build/run instructions)
- The website running locally or on GitHub Pages
- Firebase project configured with credentials (see FIREBASE_SETUP.md)

## 🧪 Test Scenarios

### Scenario 1: Sign In Flow (Authentication)

**What We're Testing:** User can sign in with Google or GitHub and get a profile created.

**Steps:**
1. Open `progress.html` in your browser
2. You should see the login screen with:
   - "Sign In with Google" button
   - "Sign In with GitHub" button
   - Link to FIREBASE_SETUP.md

3. Click **"Sign In with Google"**
4. Complete Google authentication in the popup
5. Verify:
   - ✅ You're redirected to dashboard (loginScreen hidden, dashboardScreen shown)
   - ✅ Your email is displayed in the top-right
   - ✅ Progress bars show "Not Started" for all crafts
   - ✅ Console shows "✅ User logged in: [your-email]"
   - ✅ Console shows "🆕 Creating new user profile"
   - ✅ Console shows "✅ New user profile created"

**Expected Result:**
```
✅ User logged in: yourname@gmail.com
🆕 Creating new user profile
✅ New user profile created
📊 Updating progress bars from data...
✅ Leaderboard loaded: 0 users
```

**Troubleshooting:**
- If sign-in popup is blocked: This is normal on localhost. Wait for redirect.
- If you see "Firebase Not Configured" error: Update firebaseConfig in progress.html
- If you see permission errors: Check Firestore security rules are set correctly

---

### Scenario 2: Progress Bar Calculations

**What We're Testing:** Progress bars calculate and display correctly based on completed phases.

**Test Case 1: Craft #0 (1 phase)**

1. In browser console, run:
```javascript
// Simulate completing craft0
const userId = firebase.auth().currentUser.uid;
const docRef = db.collection('users').doc(userId);
await docRef.update({
    'crafts.craft0.completed': true,
    'crafts.craft0.phases': [1]
});
```

2. Refresh progress.html
3. Verify Craft #0 progress bar:
   - ✅ Width is 100%
   - ✅ Status shows "✅ Complete"
   - ✅ Bar is green gradient

**Test Case 2: Craft #1 (8 phases, partial completion)**

1. In console, complete 4 out of 8 phases:
```javascript
const userId = firebase.auth().currentUser.uid;
const docRef = db.collection('users').doc(userId);
await docRef.update({
    'crafts.craft1.phases': [1, 2, 3, 4],
    'crafts.craft1.completed': false
});
```

2. Refresh progress.html
3. Verify Craft #1 progress bar:
   - ✅ Width is 50% (4/8)
   - ✅ Status shows "50% Complete"
   - ✅ Bar is orange gradient (in progress)

**Expected Progress Calculation:**
- Craft #0: (completed_phases / 1) * 100 = percentage
- Craft #1-5: (completed_phases / 8) * 100 = percentage
- Craft #2: (completed_phases / 3) * 100 = percentage

---

### Scenario 3: Craft Completion (Mark as Complete Button)

**What We're Testing:** Clicking "Mark as Complete" saves progress to Firestore.

**Steps:**

1. Open craft1.html
2. Scroll to bottom, find "✅ Completed This Craft?" section
3. Click **"Mark Craft #1 as Complete"** button
4. You should see alert: **"🎉 Great job! Your progress has been saved to your dashboard."**
5. Check browser console:
   - ✅ Shows "✅ Progress saved to Firestore!"

6. Go back to progress.html (Refresh)
7. Verify:
   - ✅ Craft #1 shows "✅ Complete"
   - ✅ Progress bar is 100% (green)
   - ✅ Stats show "Crafts Completed: 1/6"

**Test Offline Mode:**
1. Disconnect internet
2. Open craft0.html and click "Mark Craft #0 as Complete"
3. See alert: **"✅ Marked as complete locally! Sign in to sync to your dashboard."**
4. Check localStorage:
```javascript
JSON.parse(localStorage.getItem('completedCrafts'))
// Should show: { craft0: { completedAt: "...", markedComplete: true } }
```
5. Reconnect internet, refresh progress.html
6. Verify progress synced

---

### Scenario 4: Benchmark Tool

**What We're Testing:** Performance metrics are captured and saved.

**Prerequisites:**
- Systems Craft server running on http://localhost:8080
- /metrics endpoint accepting POST requests

**Steps:**

1. Open craft1.html
2. Click **"📈 Run Benchmark Tool"** button
3. In benchmark.html:
   - Server URL: `http://localhost:8080`
   - Clients: 10
   - Requests: 10
4. Click **"Start Benchmark"**
5. Verify:
   - ✅ Progress bar fills to 100%
   - ✅ Status message shows: "✅ Benchmark Complete! 100/100 successful"
   - ✅ Metrics grid shows: Throughput (RPS), Avg Latency, Success Rate
   - ✅ Metrics table shows detailed stats (p50, p95, p99, etc.)

6. Click **"💾 Save Metrics to Progress"**
7. See alert: **"🎉 Great job! Your progress has been saved to your dashboard."**
8. Go to progress.html and refresh
9. Verify:
   - ✅ Stats show "Best RPS: ~X RPS" (from benchmark)

**Example Results:**
```
Throughput: 45.23 RPS
Avg Latency: 2.31 ms
Success Rate: 100%

Detailed Metrics:
- Total Requests: 100
- Min Latency: 0.5 ms
- P50 Latency: 1.8 ms
- P95 Latency: 5.2 ms
- P99 Latency: 8.1 ms
- Max Latency: 12.3 ms
```

---

### Scenario 5: Leaderboard Loading

**What We're Testing:** Leaderboard displays real data from Firestore.

**Setup (requires 2+ users):**

1. Have 2 devices/browsers signed in as different users
2. Each user completes a craft and runs benchmark
3. User 1: Mark Craft #0 complete (0 RPS)
4. User 2: Mark Craft #1 complete, run benchmark with 1000+ RPS

**Expected Behavior:**

On progress.html, leaderboard should show:
```
Rank  | Name           | Crafts | RPS     | Status
🥇 1  | user2@...      | 1/6    | 1000 RPS | In Progress
🥈 2  | user1@...      | 1/6    | — RPS    | In Progress
```

**Console Output:**
```
✅ Leaderboard loaded: 2 users
```

**Test with Placeholder Data:**
If only one user, you'll see:
```
Rank  | Name           | Crafts | RPS   | Status
📍 1  | yourname@...   | 0/6    | — RPS | In Progress
```

---

### Scenario 6: Statistics Panel

**What We're Testing:** Stats are calculated correctly from progress data.

**Expected Stats Display:**

After completing some crafts:
```
Total Time Spent: — hours (manual entry, not auto-calculated yet)
Crafts Completed: X/6
Best Performance: XXXX RPS (from benchmark)
Current Rank: #1 (from leaderboard)
```

**Manual Test:**
1. Update totalTime via console:
```javascript
const userId = firebase.auth().currentUser.uid;
await db.collection('users').doc(userId).update({
    totalTime: 32
});
```
2. Refresh progress.html
3. Stats should show "Total Time Spent: 32 hours"

---

### Scenario 7: Achievement Badges (UI Test)

**Current State:** Achievement section is UI-only (not auto-triggered yet).

**What We're Testing:** Achievement display and styling.

**Steps:**

1. Look at "🏆 Achievements" section on progress.html
2. Verify three badges display:
   - ✅ 🎓 PoC Builder
   - ✅ ⚡ Optimization Pro
   - ✅ 📤 Queue Master
3. Styling should match craft progress cards

**Note:** Achievements don't auto-trigger yet. This would require:
- Craft #0 complete → PoC Builder 🎓
- Craft #1 8 phases + RPS > 1000 → Optimization Pro ⚡
- Craft #2 complete → Queue Master 📤

---

## 🔍 Debugging Checklist

### If Sign-In Doesn't Work

```
❌ Problem: "TypeError: Cannot read property 'signInWithPopup' of undefined"
✅ Solution: Check Firebase is initialized (check browser console for errors)

❌ Problem: "Invalid client ID" error from Google
✅ Solution: Update firebaseConfig with correct credentials from Firebase Console

❌ Problem: Popup blocked
✅ Solution: On localhost, Firebase automatically redirects instead
```

### If Progress Doesn't Save

```
❌ Problem: "Permission denied" error
✅ Solution: Check Firestore security rules allow authenticated reads/writes

❌ Problem: No data shows in leaderboard
✅ Solution: Make sure Firestore rules allow public reads (or test with same user)

❌ Problem: Progress bars don't update after marking complete
✅ Solution: Hard refresh (Ctrl+Shift+R or Cmd+Shift+R)
```

### If Benchmark Fails

```
❌ Problem: "CORS error" or "Failed to fetch"
✅ Solution: Ensure server is running on http://localhost:8080

❌ Problem: "All requests failed"
✅ Solution: Check server's /metrics endpoint accepts POST requests

❌ Problem: Very high latency (100ms+)
✅ Solution: Server might be overloaded. Try fewer clients (5) first
```

---

## 📊 Test Results Template

```
Date: __________
Tester: __________

✅ Authentication
- [ ] Google sign-in works
- [ ] GitHub sign-in works
- [ ] User profile created on first login
- [ ] Email displayed correctly
- [ ] Logout works

✅ Progress Bars
- [ ] Craft #0 (1 phase) calculates correctly
- [ ] Craft #1 (8 phases) partial progress shows
- [ ] Craft #1 full progress shows 100%
- [ ] Colors change: gray → orange → green
- [ ] Status text updates correctly

✅ Craft Completion
- [ ] Mark complete button appears on craft pages
- [ ] Saves to localStorage (offline)
- [ ] Syncs to Firestore (online)
- [ ] Progress dashboard updates

✅ Benchmark
- [ ] Loads with default server URL
- [ ] Progress bar animated
- [ ] Results displayed in grid + table
- [ ] Saves metrics to Firestore
- [ ] Best RPS shows on dashboard

✅ Leaderboard
- [ ] Displays top users sorted by RPS
- [ ] Shows crafts completed count
- [ ] Shows current user with highlight
- [ ] Updates when new benchmarks submitted

✅ Statistics
- [ ] Crafts completed count correct
- [ ] Best RPS from benchmarks
- [ ] Current rank from leaderboard
```

---

## 🚀 Next Steps

After all tests pass:
1. Deploy website to GitHub Pages
2. Share progress.html link with learners
3. Have them sign in and start crafts
4. Monitor leaderboard for competition!

---

## 🐛 Known Limitations

Currently not implemented:
- Auto-time tracking (need to manually set totalTime)
- Achievement auto-unlock (need to implement triggers)
- Detailed user profiles with submission history
- Private progress (leaderboard is read-only public for now)
- Mobile app version

---

**Questions?** Check [GitHub Issues](https://github.com/kapil0x/systems-craft/issues) or [Discussions](https://github.com/kapil0x/systems-craft/discussions)
