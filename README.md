# Postgresql Sidekick

This is a small collection of programs that allow a postgresql database to
trigger actions outside the database and to schedule commands to be run in
the future, without installing any extensions into the database itself.

This makes it a particularly useful approach for postgresql installations
where extensions can't easily be installed, because they're a hosted service
without shell access, or they're on a secured network where third-party
extensions, or extensions not in the vendors repo, aren't allowed, or where
the risk of instability due to potentially unstable extensions is too risky.

## How does the magic happen?

The core programs connect to the database as a normal client, and listen for
notifications sent from SQL using the
[NOTIFY](http://www.postgresql.org/docs/current/static/sql-notify.html)
command or the pg_notify() function.

### pglisten

`pglisten` listens to one or more notification channels, specified with one
or more `--listen=channel` arguments, and prints the payload of each
notification it receives to stdout.

This is mildly useful on it's own for watching for notifications, for debugging
or for ad-hoc logging, but is really useful when combined with some shell
scripting.

By default notification payloads are separated with newlines, but the `-0` or
`--print0` flag will cause them to be separated with an ASCII NUL instead
(much the same as the `-print0` flag for the `find` commmand). This allows
payloads containing newlines to be passed safely.

It can be used with xargs to perform almost any function on the payload.

As a slightly dangerous example, this ...

`pglisten --listen=del --print0 | xargs -0 -n1 rm`

... will delete /etc/passwd if you run `select pg_notify('del', '/etc/passwd').

An included sample, `delete_files.sh` lets you delete files in a particular
subdirectory (such as you might use from a delete trigger on a table that
maintains metadata information about filesystem hosted files). It also shows
how to read NUL separated arguments from a bash script:

    while IFS= read -r -d '' arg; do
      # Do something with "$arg"
    done
    
It could also be used to run external shell commands on demand, trigger
sending of email, invalidating an external cache, such as memcache, or all
sorts of other things.

### pglater

`pglater` listens to a single notification channel, called `pglater` by
default, and waits for notifications that consist of an integer and a SQL
command, separated by a space, e.g.

    30 select some_function()
    
The first parameter is the number of seconds in the future that the remainder
of the command line should be executed. In this example it will execute
`some_function()` after approximately thirty seconds.

`pglater` only keeps track of one future event at once. If it receives a
second notification it will discard any pending events. This is just enough
functionality to build an event scheduler within the database.

`pgcron.sql` is a simple scheduler based on `pglater`. It stores a list of
scheduled SQL commands in a table and runs them at the right time.

For example, this

    insert into scheduled_events (run_at, period, run_action)
        values (current_timestamp, '20 minutes',
                $$delete from spool where expires < current_timestamp$$);

will run the command to clean up the spool table immediately and then every
20 minutes.

## Security

Anyone in the database can issue a notify command. That means that any
database user can use `pglater` to run arbitrary SQL as the database user
that `pglater` runs as. That means that the user `pglater` connects as should
have no more privileges than the least privileged user. (Security definer
functions could be used, with some sanity checking, to allow more access
for specific operations).

That also means that any database user can cause `pglisten` to print out any
string. If it's being used with a script that performs any destructive or
security related action it should include some sanity checking.

## Building

Run `make` to build both binaries.

This was developed on OS X and Linux, and should build with minor effort on
anything unix-ish. Porting to Windows would take a little more effort.
