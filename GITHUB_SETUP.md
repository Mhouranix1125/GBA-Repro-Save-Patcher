# Publishing this folder to GitHub

This folder is already structured as the repository root.

Typical first commit:

```bash
git init
git add .
git commit -m "Initial release v0.4.3"
git branch -M main
git remote add origin <your-repository-url>
git push -u origin main
```

Do not manually add generated files ignored by `.gitignore`.

Compiled release executables should preferably be attached to a GitHub Release rather than committed to the main source tree.
