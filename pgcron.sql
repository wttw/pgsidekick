-- -*-sql-*-
--
-- Simple scheduling system designed for use with pglater.
--
-- Insert into scheduled_events table to schedule an event - when the time reaches
-- approximately run_at, the sql commmand in run_action will be exectuted. If
-- period is not null, it will be added to run_at to reschedule the event to run
-- in the future.
--
drop table if exists scheduled_events;
begin;
create table scheduled_events (
  id serial not null,
  run_at timestamptz not null,
  period interval,
  run_action text not null
);

create index scheduled_events_run_at_idx on scheduled_events(run_at);

create or replace function run_events() returns integer as $$
  declare
    ev record;
  begin
    loop
      select * into ev from scheduled_events where run_at <= current_timestamp order by run_at desc limit 1;
      if not found then
        exit;
      end if;
      begin
        execute ev.run_action;
      exception when others then
        raise notice 'pgcron: run_events error in "%" % %', ev.run_action, SQLERRM, SQLSTATE;
      end;
      if ev.period is null then
        delete from scheduled_events where id = ev.id;
      else
        update scheduled_events set run_at = ev.run_at + ev.period where id = ev.id;
      end if;
    end loop;
    select cast(ceil(extract(epoch from run_at) - extract(epoch from current_timestamp)) as integer) as delta into ev from scheduled_events order by run_at desc limit 1;
    if found then
      perform pg_notify('pglater', ev.delta::text || ' select run_events()');
      return ev.delta;
    end if;
    return null;
  end;
$$ language plpgsql;

create or replace function events_changed() returns trigger as $$
  begin
    perform run_events();
    return NEW;
  end;
$$ language plpgsql;

create trigger events_changed
  after insert or update on scheduled_events
  for each statement
  execute procedure events_changed();

commit;
