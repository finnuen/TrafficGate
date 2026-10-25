# Global instructions

## Ask when confused; never assume

If a request can be read more than one way, or a fact needed to act on it is
unknown, ask — before touching a file. One short question costs a minute; a
wrong assumption about which app, which repository, or which file costs the work
and the checkpoint that would have prevented it.

Never invent a path, a version, a config key, or a build command to keep moving.
"I don't know which of the two PepperCut trees you mean" is a good answer;
guessing is not, and staying silent about an assumption is the same as making
one.

## Open with a TLDR

When an answer is long or has several parts — more than a short paragraph, or
anything with subsections, a table or a list — start it with a one or two line
summary:

    **TLDR:** the answer, in one sentence.

Skip it for a one-line reply or a single short paragraph, where it would only
repeat what follows. The summary goes at the top, so it can be read instead of
the whole answer.

## Locating a file the user mentions

When the user refers to a file without giving a full path — "put this in
agents", "build the zip", "fix the README" — check the Desktop first:

    C:\Users\kuuha\Desktop

Files handed over usually land there: exports, handover notes, downloaded
zips, screenshots. Only widen the search if it is not on the Desktop.

## Binary names carry the version

Every executable a build produces is named with the version it contains:

    app_v1.0.exe
    app_v1.0.0.exe

Lowercase `v`, an underscore before it, no spaces.

The version bumps itself, so nobody has to remember an edit before building. A
build that carries a change bumps PATCH; a minor or major bump is the deliberate
act. The project's build script owns that number in one place and writes every
copy of it — file version, product version, the binary name, and whatever the app
itself displays — so the tag, the release asset and the running app cannot
disagree. Anything landing in `build/`, `dist/`, `release/`, or wherever a
release executable is written follows this.

Two escape hatches, and only two: a flag that rebuilds the current version
unchanged, and a flag that sets the version outright.

Existing projects that bump by hand get the auto-bump the first time a prompt
touches them.

A project that already names its binaries with three digits — `v<major>.<minor>.<patch>`
— keeps three. Anything new starts at two — `v<major>.<minor>`.

Settled so far:

    peppersnap    three digits   PepperSnap_v4.2.1.exe
    PepperStitch  three digits   PepperStitch_v1.0.0.exe
    PepperCut     two digits     PepperCut_v1.0.exe
    DL-Calc       three digits   DL-Calc_v2.1.0.exe

A project not listed here is either already three-digit, in which case it stays
that way, or new, in which case it starts at two. DL-Calc is the one exception
recorded so far: it is new, but starts at three digits because it inherits the
Windows 2.0.0 line it replaces.

## Release assets: the binary, nothing else

A release attaches the executable and nothing else. No source zip, no copy of the
repository, no documentation bundle: the source is already public at the released
tag, so a second copy can only drift from it and mislead whoever downloads it.

An older release that carries one is not a precedent, and "the last release had
it" is not a reason to add another.

## Standalone, not dependency-free

A release is one file that runs: no installer, no admin rights, no DLL sitting
beside it, nothing to install before it works. That is the promise. "No
third-party code at all" is not.

New projects may take on a runtime the machine is likely to have already —
WebView2, .NET, the VC++ redistributable — and should embed whatever can be
embedded (the WebView2 loader as RCDATA, the way PepperStitch and JustViewMD
do). What they may not do is ship a second file or an installer.

Existing projects keep the stricter rule until the user says otherwise for one of
them by name: peppersnap and DL-Calc-windows link nothing but Windows system
libraries, and stay that way.

Be accurate about it in the README. Embedding the loader does not embed the
runtime. "Requires the WebView2 Runtime, included with Windows 11" is honest;
"no dependencies" on a project that needs one is not.

## Checkpoint every project before changing it

Before the first file a prompt will modify, archive the project you are about to
touch. The archive is the only thing standing between a wrong edit and an
unrecoverable one, and it costs about a second.

### Where checkpoints live

One folder per project, side by side under:

    C:\Users\kuuha\Documents\Opencode\<Project>-checkpoints

The checkpoints folder always goes in that directory, wherever the project
itself lives — a repository on another drive still checkpoints here, so every
checkpoint on the machine is in one place. The folder name follows the project's
own name (`PepperSnap` for the directory `peppersnap`). NTFS is case-insensitive,
so that casing is cosmetic: the same folder is found either way.

### What a checkpoint is

A zip of the **entire** project folder, verbatim:

    C:\Users\kuuha\Documents\Opencode\PepperSnap-checkpoints\PepperSnap_v4.2.0-checkpoint-261020-0600.zip

The name is `<Project>_v<Version>-checkpoint-<YYMMDD-HHMM>.zip` — the project's
own version with the `v` its releases carry, an underscore, then two-digit year,
month and day, then 24-hour hour and minute, in local time. The timestamp is the
moment the **user sent the prompt**, not the moment the archive is written; a
prompt that arrives at 10:04 and is worked on at 11:20 is still `261020-1004`.

Read `<Version>` from the project at the moment of archiving rather than
remembering it (`APP_VERSION` in `PepperSnapInternal.h` here). The checkpoint is
taken *before* the first edit, so it carries the version the archive actually
contains: a checkpoint taken just ahead of a version bump keeps the number it was
at, which is the honest label for what is inside it. If a project records no
version at all, drop the segment and fall back to the older
`<Project>-checkpoint-<YYMMDD-HHMM>.zip` form.

Checkpoints taken before this rule keep the names they were given; never rename
an existing archive to match a newer naming rule, because its name is the only
record of what it was taken for.

Nothing is excluded: `.git`, build output, binaries, assets, all of it. That
matters more than the disk it costs.

- `.git` is what makes the archive a complete repository rather than a pile of
  files. History, tags, and the difference between committed and uncommitted work
  all come back with it.
- An exclusion list is a guess about what will matter, and the file that turns
  out to matter is the one guessed wrong. Build output is often the only
  surviving copy of a binary that worked.

### When to take one

Once per prompt that will change files, immediately before the first change. A
prompt that only reads — a question, a summary, a code search — changes nothing
and needs no snapshot. A prompt with several separate edits gets one checkpoint
covering all of them, not one per edit.

If the chosen name already exists, that checkpoint is from an earlier state and
must not be overwritten: append `-2`, `-3`, and so on.

### Never prune them

Checkpoints are kept for ever and never deleted, not even when the work they
preceded is long since committed. They are cheap (a 15 MB project zips to about
13 MB) and the point of them is that an old one still exists when something is
found to be wrong months later.

### Copy with ZipFile.CreateFromDirectory, never Compress-Archive

`Compress-Archive` silently skips files and directories carrying the Windows
Hidden attribute. That is not a corner case: `.git` is normally hidden, so
`Compress-Archive -Path .\project\*` produces an archive that looks fine, reports
no error, and contains no history at all. Measured here on a four-file test
folder, it archived two — it dropped both the hidden `.git` directory and a
hidden file. (A dot-prefixed name like `.gitignore` is not hidden on Windows, so
that one survived; it is the attribute that decides.)

```powershell
Add-Type -AssemblyName System.IO.Compression.FileSystem

$project = 'peppersnap'          # the project directory, under Documents\Opencode
$display = 'PepperSnap'          # its own name, as the project writes it
$version = 'v4.2.0'              # the version the project records, read not recalled
$stamp   = '261020-0600'         # the moment the user sent the prompt

$src = "C:\Users\kuuha\Documents\Opencode\$project"
$out = "C:\Users\kuuha\Documents\Opencode\$display-checkpoints"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$zip = Join-Path $out "${display}_$version-checkpoint-$stamp.zip"
for ($i = 2; Test-Path $zip; $i++) { $zip = Join-Path $out "${display}_$version-checkpoint-$stamp-$i.zip" }

[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $src, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $true)
```

`CreateFromDirectory` takes everything, hidden or not.

### Verify the archive, then say so

An unchecked archive is an assumption, and its failure is silent: a zip that
stopped short still opens and still looks plausible. Count what went in against
what arrived:

```powershell
$in  = (Get-ChildItem $src -Recurse -File -Force).Count
$z   = [System.IO.Compression.ZipFile]::OpenRead($zip)
$got = ($z.Entries | Where-Object { $_.Name -ne '' }).Count
$z.Dispose()
"$in source files, $got in the zip, match: $($in -eq $got)"
```

Tell the user the checkpoint's name and that it verified before starting the
work.

### If the archive cannot be written

A running program can hold a file open — a built exe especially — and the zip
then fails. Quit the program and retry; never checkpoint a partial tree and call
it done. For a project large enough that this is slow (say a gigabyte or more),
say so in one line before archiving rather than silently skipping it.

### Restoring one

Close anything touching the project first, since open handles block the write.
Expand the zip over the project folder with Explorer or `Expand-Archive`, or into
a scratch directory to compare against and copy back selectively. A checkpoint is
not a substitute for committing: it captures the folder as it stood, including
work that was never committed.

## Deleting files: always use the Recycle Bin

Never delete a file or directory permanently. Always send deletions to the
Windows Recycle Bin so the user can restore them. This applies to every
session, every project, and every kind of file, including scratch and
temporary work.

`Remove-Item`, `del`, `rm`, `rd`, `rmdir`, and `cmd /c del` bypass the Recycle
Bin. Do not use them to delete anything. Use the Microsoft.VisualBasic file
API instead:

```powershell
Add-Type -AssemblyName Microsoft.VisualBasic

# A file
[Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile(
    $path, 'OnlyErrorDialogs', 'SendToRecycleBin')

# A directory (recursive)
[Microsoft.VisualBasic.FileIO.FileSystem]::DeleteDirectory(
    $path, 'OnlyErrorDialogs', 'SendToRecycleBin')
```

Then confirm the item is gone with `Test-Path $path`.

When a tool or script needs a temp path cleaned up, use this same method
rather than a plain filesystem delete.

### When the Recycle Bin cannot be used

The Recycle Bin cannot hold items on network shares / UNC paths, items on
drives that have no Recycle Bin (some removable media), or items larger than
the drive's Recycle Bin size limit. If a deletion falls into one of those
cases, do not silently fall back to permanent deletion: stop and ask the user
how to proceed.

## Git: pushing to GitHub

Three standing rules, applying to every repository and every session:

### Never add AI tooling as a contributor

Do not attribute the work to DeepSeek, OpenCode, GitHub Copilot, or any other
AI model, agent, or coding tool. Concretely, do not:

- add a `Co-Authored-By: <tool>` trailer to commit messages
- add a "Generated with ..." or similar footer to commit messages or PR bodies
- list any AI tool in `CONTRIBUTING`, `AUTHORS`, or similar files

Commits go out under the user's own name and email only.

### Push directly to `main`

When asked to push, commit and push straight to `main`. Do not create a
feature branch. Do not open a pull request.

Because there is no review gate, show `git status` and the staged diff before
committing, so the user can confirm exactly what is about to land on `main`.
There is no PR to close if something is wrong; recovery is a revert commit.

### Do not push until the user says to push

Never run `git push` on your own initiative, and never as the automatic final
step of a task. "Commit this", "fix it", "add the note", "finish it" are not
permission to push. Prepare the work, show what would land, then stop and wait
for an explicit push instruction — "push", "push it", "ship it".

This gate exists because a push to `main` is published and permanent in a way
a local commit is not. Whatever was pushed is already served, archived and
crawled by the time a mistake is noticed.

## Deleting or recreating a GitHub repository

Learned the hard way on `finnuen/ShutdownTimer`. Deleting a repository and
recreating it destroys much more than the git history, and some of it cannot
be recovered.

### Never delete the original until the replacement is verified

Keep both repositories alive at the same time by rebuilding under a
placeholder name:

1. Copy the original completely, including the release asset **files**.
2. Create the replacement under a placeholder name (two repositories cannot
   share a name, so a placeholder is what lets both exist at once).
3. Push, then prove it matches the original — commits, branches, tags,
   releases **and assets**. Compare counts, do not eyeball it.
4. Only after that comparison passes, delete the original.
5. Rename the placeholder to the original name.

This ordering also removes the dependency on repo-delete permission, because
the irreversible step happens last and can be done by whoever holds it.

The point is that the source stays alive until the destination is proven. Once
the original is deleted, anything that was missed is unrecoverable, and
reusing the name immediately closes GitHub's 90-day restore window. Verifying
"it's exactly the same" is what forces the asset check; without that step a
placeholder rebuild silently loses binaries just as easily.

### Back up everything first, including binaries

Before a repository is deleted, capture all of:

- release **asset files** — actually download them, not just their metadata
- releases (tag name, name, body, draft/prerelease flags)
- issues and pull requests (title, body, comments)
- repository settings (description, default branch, visibility, topics)

Reusing the repository name permanently closes GitHub's 90-day restore window,
so any file that was not downloaded beforehand is gone for good.

### A `Co-authored-by:` trailer makes that account a contributor

A `Co-authored-by:` trailer credits the named account as a repository
contributor. Rewriting the default branch and force-pushing does **not** remove
it: merged pull requests keep their head commits forever under
`refs/pull/N/head`, those refs cannot be deleted by users, and merged pull
requests cannot be deleted. The commit therefore still exists on GitHub even
though it is unreachable from any branch.

### Verify against the right endpoint

GitHub has several, disagreeing sources of contributor data. The repository
sidebar (About panel) is served by:

    GET https://github.com/{owner}/{repo}/_sidebar/contributors

That endpoint can list a contributor which neither
`/repos/{owner}/{repo}/contributors` nor `/graphs/contributors-data` shows.
Always check the `_sidebar` one. It sends `cache-control: no-cache`, so a
discrepancy there is server-side state, not a browser or CDN cache.

The reliable ways to purge a stuck contributor are recreating the repository,
or asking GitHub Support to recompute the repository's contributor data.

### Fix attribution before the first push

An unwanted trailer is cheap to remove with `git commit --amend` while the
commit is still unpushed. Once it has gone through a pull request, it is
permanent.

### README images can die with the repository

Images uploaded through GitHub's editor are stored as
`github.com/user-attachments/assets/{uuid}` and are deleted along with the
repository (the URL then returns 404). Commit documentation images into the
repository and reference them by relative path instead.

