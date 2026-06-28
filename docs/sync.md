# Docs Sync Setup

## 1. Create the docs repo

    https://github.com/organizations/palmshed/repositories/new

    Repo name: cursor-docs
    Visibility: public (for Pages)

## 2. Create a GitHub PAT

    Settings > Developer settings > Personal access tokens > Fine-grained tokens

    Repository access: Only select repositories → palmshed/cursor-docs
    Permissions: Contents (Read and write)

## 3. Add the PAT as a repo secret in the main repo

    Settings > Secrets and variables > Actions > New repository secret

    Name: DOCS_SYNC_TOKEN
    Secret: (the token from step 2)

## 4. Enable Pages on the docs repo

    In palmshed/cursor-docs: Settings > Pages > Source: GitHub Actions

## 5. Add the Pages workflow to the docs repo

Push this as `.github/workflows/pages.yml` in the docs repo:

```yaml
name: Pages
on:
  push:
    branches: [main]
  workflow_dispatch:
permissions:
  contents: read
  pages: write
  id-token: write
  deployments: write
concurrency:
  group: pages
  cancel-in-progress: false
jobs:
  deploy:
    runs-on: ubuntu-latest
    environment:
      name: github-pages
      url: ${{ steps.deployment.outputs.page_url }}
    steps:
      - uses: actions/checkout@v4
      - uses: actions/configure-pages@v6
      - uses: actions/upload-pages-artifact@v3
        with:
          path: '.'
      - id: deployment
        uses: actions/deploy-pages@v4
```
