# Publishing the repository

The archive is prepared for a new repository; it has not been uploaded to GitHub. Publish the contents of the directory containing README.md so `.github`, `components`, `panel`, and `detector` are at the repository root.

## Local Git setup

From that directory:

```sh
git init -b main
git add .
git diff --cached --stat
git commit -m "Add ESP32 wireless motion alarm with latched LED output"
```

If Git asks for an identity, configure your own name and preferred commit email. Do not copy another author's identity.

Create an empty GitHub repository, then use the remote URL GitHub gives you:

```sh
git remote add origin https://github.com/YOUR-USERNAME/esp32-wireless-alarm.git
git push -u origin main
```

Replace YOUR-USERNAME. Do not initialize the remote with a separate README if following this sequence. Authenticate through your normal Git credential manager; never put a token in the source or remote URL.

## Before sharing an application link

- Inspect the GitHub Actions run. It must actually pass before describing CI as passing.
- Flash the LED release and record the bench acceptance results.
- Add a wiring photo and a short demonstration showing ARM, motion, persistent ALARM after CLEAR, DISARM and link loss.
- Choose a license if you want to grant reuse rights. This archive intentionally does not assign an author or license on your behalf.
- Keep claims tied to what you tested. Do not claim measured response time, range, encryption, certification, or independent authorship of assisted code without supporting evidence.

Suggested repository description: "Two-node ESP32 motion alarm in C/ESP-IDF with ESP-NOW messaging, OLED status, arm/disarm controls and a latched LED alarm."

## Repository hygiene

The included `.gitignore` excludes build products, local SDK configuration and editor state. Commit `sdkconfig.defaults`, source, tests and documentation. Keep raw captures only when useful and reviewed. This workflow uses GitHub's read-only contents permission and never flashes a physical board.
