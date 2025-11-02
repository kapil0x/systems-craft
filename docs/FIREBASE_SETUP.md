# Firebase Setup Guide for Systems Craft

This guide walks you through setting up Firebase to power the enrollment and progress tracking system for Systems Craft.

## 📋 Prerequisites

- A Google account
- A Firebase project (free tier is sufficient for learning)
- 15 minutes for setup

## 🚀 Step 1: Create a Firebase Project

1. Go to [Firebase Console](https://console.firebase.google.com/)
2. Click **"Create a project"**
3. Enter project name: **"systems-craft"**
4. Choose a Google Cloud project location (any region is fine)
5. Accept the terms and click **"Create Project"**
6. Wait for the project to be created (2-3 minutes)

## 🔧 Step 2: Get Your Firebase Configuration

1. In Firebase Console, click your project
2. Click the **gear icon** → **"Project settings"**
3. Scroll to **"Your apps"** section
4. Click **"Web"** icon (if no apps yet, click "Add app")
5. Register with app name: **"systems-craft-web"**
6. You'll see a configuration object:

```javascript
const firebaseConfig = {
  apiKey: "YOUR_API_KEY_HERE",
  authDomain: "YOUR_PROJECT_ID.firebaseapp.com",
  projectId: "YOUR_PROJECT_ID_HERE",
  storageBucket: "YOUR_PROJECT_ID.appspot.com",
  messagingSenderId: "YOUR_MESSAGING_SENDER_ID",
  appId: "YOUR_APP_ID"
};
```

7. **Copy these values** and save them temporarily

## 🔐 Step 3: Enable Google OAuth

1. In Firebase Console, go to **Authentication** (left sidebar)
2. Click **"Get Started"** if prompted
3. Click **"Sign-in method"** tab
4. Click **"Google"** and enable it
5. Set **Project support email** (any email is fine)
6. Click **"Save"**

## 🔑 Step 4: Enable GitHub OAuth (Optional but Recommended)

1. In **Authentication** → **Sign-in method**
2. Click **"GitHub"** and enable it
3. You'll need GitHub OAuth credentials:
   - Go to [GitHub Settings](https://github.com/settings/developers)
   - Click **"New OAuth App"**
   - **Application name:** Systems Craft
   - **Homepage URL:** `https://yourdomain.com` (or localhost for testing)
   - **Authorization callback URL:** `https://YOUR_PROJECT_ID.firebaseapp.com/__/auth/handler`
   - Click **"Register application"**
   - Copy **Client ID** and **Client Secret**
4. Paste into Firebase GitHub configuration
5. Click **"Save"**

## 📊 Step 5: Create Firestore Database

1. In Firebase Console, go to **Firestore Database** (left sidebar)
2. Click **"Create database"**
3. **Security rules:** Choose **"Start in test mode"** (for development)
4. **Location:** Select nearest region
5. Click **"Enable"**

## 🛡️ Step 6: Set Firestore Security Rules

1. In **Firestore Database** → **Rules** tab
2. Replace the rules with:

```
rules_version = '2';
service cloud.firestore {
  match /databases/{database}/documents {
    // Allow users to read/write their own data
    match /users/{userId} {
      allow read, write: if request.auth.uid == userId;
    }

    // Allow anyone to read leaderboard (but not write)
    match /leaderboard/{document=**} {
      allow read: if true;
      allow write: if false;
    }
  }
}
```

3. Click **"Publish"**

## 💻 Step 7: Update progress.html

1. Open `website/progress.html` in a text editor
2. Find the `firebaseConfig` object (around line 513)
3. Replace with your actual values from Step 2:

```javascript
const firebaseConfig = {
    apiKey: "YOUR_ACTUAL_API_KEY",
    authDomain: "your-project-id.firebaseapp.com",
    projectId: "your-project-id",
    storageBucket: "your-project-id.appspot.com",
    messagingSenderId: "YOUR_ACTUAL_MESSAGING_ID",
    appId: "YOUR_ACTUAL_APP_ID"
};
```

4. **Save the file**

## 🧪 Step 8: Test the Setup

1. Open `website/progress.html` in a browser
2. You should see a login prompt
3. Click **"Sign in with Google"** or **"Sign in with GitHub"**
4. After signing in, you should see the progress dashboard
5. Refresh the page - your progress should persist

## ✅ Troubleshooting

### "Sign-in with popup has been blocked"
- This is normal in localhost - Firebase will redirect instead
- The system handles this automatically

### "User doesn't have permission"
- Check Firestore security rules (Step 6)
- Make sure you published the rules

### "Project quota exceeded"
- Firebase free tier allows 100 simultaneous users
- This is plenty for learning

### Configuration not working
- Check browser console (F12) for errors
- Verify all configuration values are correct
- Make sure Firestore database is created

## 📈 What's Next

Once Firebase is configured:

1. **Add "Mark Complete" buttons** to craft pages (they'll sync progress)
2. **View your progress** in the dashboard
3. **Share your achievements** with others on the leaderboard

## 🔗 Useful Links

- [Firebase Documentation](https://firebase.google.com/docs)
- [Firestore Data Model](https://firebase.google.com/docs/firestore/data-model)
- [Firebase Authentication](https://firebase.google.com/docs/auth)
- [Firebase Security Rules](https://firebase.google.com/docs/rules)

---

**Questions?** Check the [GitHub Issues](https://github.com/kapil0x/systems-craft/issues) or [Discussions](https://github.com/kapil0x/systems-craft/discussions)
