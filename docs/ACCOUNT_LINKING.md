# Account linking (future, optional) - design only

Status: **design only, not built.** Guest login stays the default and
nobody is ever forced to link.

## Why

Today a guest player lives in one browser (an id + a secret token). If the
browser data is cleared or the phone is lost, the player, their rating and
history are gone. Linking lets a player keep their profile.

## What the player sees

1. Lobby → **Keep my player safe** (optional).
2. Choose **Google** or **Email**.
   - Google: the normal "Sign in with Google" screen.
   - Email: type an email address, receive a 6-digit code, type it in.
3. Done: "Your player is now linked to …". Nothing else changes.
4. On a new phone: **I already have a player** → Google / Email → the same
   player, rating and history come back.

## How it works (server)

- New table `links`: `(account, provider, subject, linkedAt)`.
  - Google: `subject` = Google's stable user id (`sub` from the verified ID
    token), never the email.
  - Email: `subject` = SHA-256 of the lower-cased email + server pepper.
    The plain email is stored only if the user agrees to receive messages.
- One account per Google id / email; one link of each kind per account.
- **Linking** needs the current guest login (proves you own the player)
  plus a fresh proof from the provider.
- **Signing in** on a new device with a linked provider gives that device a
  **new** guest token for the same account (old devices keep working until
  the player taps "sign out everywhere", which rotates all tokens).
- Unlinking requires a fresh provider proof; an account must keep at least
  its guest token.
- Rate limits: 5 email codes per address per hour; codes expire in 10
  minutes; 5 wrong codes burn the code.
- Bans follow the account, not the device: linking a new device to a banned
  account shows the ban.

## Costs and approvals needed before building

| Item | Cost | Needs your approval |
|---|---|---|
| Google sign-in (Google Identity Services, OAuth client) | free | yes - a Google Cloud project and OAuth consent screen in your name |
| Sending email codes | free tiers exist (limited per day); real volume usually costs money | yes - choose a provider; no paid plan without approval |
| Privacy policy page | ₹0 (text) | yes - it must describe what is stored |
| HTTPS (already required) | ₹0 (tunnel / Let's Encrypt) | - |

## Security notes

- Verify Google ID tokens on the server (signature, audience = our client
  id, issuer, expiry). Never trust a token the browser "decoded".
- Email codes: random 6 digits, stored hashed, constant-time compare.
- Store as little personal data as possible (DPDP Act 2023, India): only
  the provider id / email hash, unless the player opts in to more.
- Account deletion: unlink + delete personal data on request; keep match
  records anonymised (leaderboards stay correct).

## Order of work (when approved)

1. Data model + "sign out everywhere" (token rotation) - no external service.
2. Email codes with a free-tier provider (approval first).
3. Google sign-in (approval first: Google Cloud project).
4. Privacy policy, deletion request flow.
