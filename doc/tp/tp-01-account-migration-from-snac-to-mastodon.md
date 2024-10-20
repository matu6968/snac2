# Test Procedure - 01. Account migration from snac to Mastodon

## Prerequisites

- A snac account in your server (from now, the *origin* account).
- A Mastodon account, already created on any public server (from now, the *destination* account).
- 3 other Fediverse accounts, already created on public servers (from now, the *follower* accounts). These accounts must be from Mastodon instances (or, optionally, from other implementations where the `Move` activity is known to be supported).

## Steps

1. From all of the follower accounts, follow the origin account.

2. Write a post from the origin account and ensure that it appears in all of the follower account's timelines.

3. Follow 3 random accounts from the origin account.

4. (Optional) create a list named `test` in the original account (must be done from a Mastodon API client app, as snac's web interface does not allow creating lists yet). Add one of the followed random accounts to the list.

5. Follow all steps from the `snac(8)` manual page, § *Migration from snac to Mastodon*.

6. From the destination account, check that the 3 random accounts that were followed by the origin account are also being followed here (this step checks the import of the `following_accounts.csv` file).

7. (Optional, depends on step 4) From the destination account, check that a list named `test` exists and that the random account added in step 4 is also in the list (this step checks the import of the `lists.csv` file).

8. From the destination account, check that the 3 follower accounts are now following this one (this step tests the `migrate` command and `Move` activity processing).

9. (Optional) From any of the follower accounts, check that the destination account is followed.
