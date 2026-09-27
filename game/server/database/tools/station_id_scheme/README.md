# Station id scheme

Without external authentication, the LoginServer turns an account name into a station id, and every character is stored under that id. The rule used to be `std::hash`, which gives different results on 32-bit and 64-bit builds. There are now two named, fixed rules:

- **gcc32**: the rule every 32-bit server used. New databases use it.
- **gcc64**: the rule used by servers built from the 64-bit branch before this was fixed. It is kept only so those databases keep working.

A database's accounts were created under one rule, so the **database** records which one, in the login table `station_id_scheme`. The LoginServer reads it at startup and will not start without it. The config key `[LoginServer] stationIdScheme` is optional and only double-checks the database; it cannot change the rule.

## New databases

`ant create_database` records `gcc32`. Nothing to do.

## Existing databases (after update 272)

Update 272 creates the table but leaves it empty, because only you or the data can say which rule your accounts were created with. Label it once:

1. Export the station ids from every schema that holds login or cluster tables (usually one schema). This only reads:

   ```
   sqlplus -s user/password@service @export.sql > ids.tsv
   ```

   With separate login and cluster schemas, run it in each and concatenate the outputs.

2. Let the tool decide from the evidence:

   ```
   stationIdScheme classify < ids.tsv > label.sql
   ```

   If `ids.tsv` is empty, `classify` will label the database as new, so first make sure you ran `export.sql` in the schema that holds `account_info` and `players`. An export from the wrong schema is empty.

   A character that has entered the world records its account name. For each station id, the tool checks which rule turns that name into that id. It records a label only when **every** station id is explained the same way.

3. If `classify` records nothing (the usual case for older servers, because characters from before 2022 and accounts without characters have no recorded name), state what you know:

   - **This server only ever ran 32-bit builds:** `stationIdScheme assert gcc32 < ids.tsv > label.sql`
   - **This server was created by the 64-bit branch before this fix:** `stationIdScheme assert gcc64 < ids.tsv > label.sql`

   The label is recorded as *asserted*, with the date. The tool refuses if any station id is proven to belong to the other rule.

4. Apply it (in the login schema):

   ```
   sqlplus -s user/password@service @label.sql
   ```

The report on stderr lists every station id the tool could not explain. Keep it with your records.

The export also lists the label already recorded, and neither `classify` nor `assert` replaces one. Only a verified re-key (below) changes a label.

If `classify` reports that **both** rules are proven, accounts were created under both (for example, a server that ran 32-bit and then the unpatched 64-bit build). Those accounts cannot share one rule without re-keying one set of them. The re-key below does not do that: it refuses any database with an account proven under gcc32. Restore from backup, or resolve those accounts by hand.

## Re-keying a gcc64 database to gcc32 (optional)

A database created by the unpatched 64-bit branch can keep running under `gcc64`. The re-key is optional: it moves every account to the id gcc32 gives its name, so that the database uses the same rule as every other server, and labels it `gcc32` (*evidence*).

It moves an account only on evidence, and it never merges two accounts. It writes a script only if **all** of the following hold:

- the database is labelled `gcc64` or not labelled;
- every station id in the export (tables and the objvars below) has a witness, a name recorded by one of its characters;
- for every id, each witness reproduces the id under gcc64 and not under gcc32, and all of them give the same gcc32 id. The account moves there. The exception is an id whose witnesses all reproduce it under both rules (numeric names), which stays;
- no new id is another account's new id, or any id already in the database.

Anything else stops it with a report, and nothing is written: an id without a witness, a witness that fits gcc32 only or neither rule, witnesses that disagree, a collision, or an id not in the form update 272 stores. Accounts without a character that has entered the world since the name was recorded have no witness, so many databases cannot be re-keyed. Keep them on `gcc64`; that is fully supported.

The script moves the station id in every place the game reads it back from (historical records that are never compared with a station id, such as the grant details on veteran reward items, keep the old id):

- the login tables `account_info`, `account_reward_events`, `account_reward_items`, `extra_character_slots`, `feature_id_transactions`, `purge_accounts` and `swg_characters`;
- `account_extract.user_id`, the account list the purge process reads. It may hold an id in its unsigned form; like the purge process, the export and the script read it folded onto the stored form, and a moved id is written in the stored form. A value that is not a 32-bit station id in either form stops the re-key, and so do two rows holding both forms of one id (keep one of them, then export again). If `[LoginServer] purgeAccountSourceTable` names a different table, re-key that table yourself. Whatever refills the account list after the re-key must use the gcc32 ids;
- the cluster tables `players`, `player_objects`, `accounts`, `temp_characters`, `account_map` (`parent_id` and `child_id`) and `character_profile`;
- the int objvars `player_structure.admin_all_characters`, `chronicles.quest_creator_station_id` and `manf.owner_station_id`, both in `object_variables` and in the packed `objects.objvar_<n>_*` slots;
- accounts on cell allow and ban lists, stored in `property_lists` (lists 3 and 4) as `A:<station id>`.

Procedure:

1. Stop every server process and take a backup (`expdp`) of every schema involved. **The backup is the only rollback** once the script has committed.

2. Export, as for labelling, from every schema that holds login, cluster or station-players tables:

   ```
   sqlplus -s user/password@service @export.sql > ids.tsv
   ```

3. Dry run. The tool only reads `ids.tsv`; it never connects to the database:

   ```
   stationIdScheme rekey < ids.tsv > rekey.sql 2> rekey-report.txt
   ```

   The report lists every move (old id, new id, the witness names and where the id is stored) or every reason it stopped. Apply nothing unless the exit status is 0. Read the report and keep it with the backup.

4. Check every schema before changing any. Write a check script from the same export and run it in each schema that holds login, cluster or station-players (`character_profile`) tables:

   ```
   stationIdScheme rekey --check < ids.tsv > rekey-check.sql
   sqlplus -s user/password@service @rekey-check.sql
   ```

   It makes every change the real script makes, then rolls back; it commits nothing. Each schema must print `check passed in this schema`. With separate schemas this matters: the real script commits schema by schema, so a problem found only in the last schema would otherwise leave the earlier ones already changed.

5. Apply the script in each schema that holds login, cluster or station-players tables. With separate schemas, do the cluster schema first and the login schema (the one with `station_id_scheme`) last:

   ```
   sqlplus -s user/password@service @rekey.sql
   ```

   In each schema the script is one transaction (`whenever sqlerror exit failure rollback`). On any error it rolls back, and nothing in that schema changes. Before changing anything it refuses if the label is not `gcc64` (or absent). While it moves the ids, it stops and rolls back if it finds a station id that was not in the export, which means the database changed after the export. The label becomes `gcc32` (*evidence*) in the same transaction, after every move. A schema that holds the login tables but no `station_id_scheme` table is refused (apply update 272 first), so the login schema can never be re-keyed without its label. If the script fails in a later schema after an earlier one committed, restore the backup.

6. Check: export again and run `stationIdScheme rekey`. It must refuse because the database is now labelled `gcc32`: there is nothing left to do. The LoginServer logs `gcc32` at startup; any `[LoginServer] stationIdScheme` assertion in the config must be changed to `gcc32`.
