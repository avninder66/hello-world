-- Alternative to Vercel Cron: have Supabase call the publisher every minute.
-- Run once in the Supabase SQL editor after deploying the app.
-- Replace the URL and secret with your APP_URL and CRON_SECRET.
create extension if not exists pg_cron;
create extension if not exists pg_net;

select vault.create_secret('REPLACE_WITH_CRON_SECRET', 'cadence_cron_secret');

select cron.schedule(
  'cadence-publish',
  '* * * * *',
  $$
  select net.http_post(
    url := 'https://YOUR-APP.vercel.app/api/cron/publish',
    headers := jsonb_build_object(
      'Authorization', 'Bearer ' || (select decrypted_secret from vault.decrypted_secrets where name = 'cadence_cron_secret')
    ),
    timeout_milliseconds := 290000
  );
  $$
);

-- To stop it: select cron.unschedule('cadence-publish');
