// The only file you need to edit to switch the site from demo mode to live.
window.PSB_CONFIG = {
  mode: 'live',                       // 'mock' = demo with fake data in your browser  |  'live' = real accounts and payments
  ambassadors: ['tg123'],             // usernames that get the black star (Ambassador) next to their name
  siteName: 'Purple SoundBank',
  price: '£5',
  supabaseUrl: 'https://qfwfwypkpzonsajhxeaz.supabase.co',                    // e.g. https://abcdxyz.supabase.co   (live mode only)
  supabaseAnonKey: 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InFmd2Z3eXBrcHpvbnNhamh4ZWF6Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3OTQzNTcyMDcsImV4cCI6MjEwOTkzMzIwN30.BK-vOd5VQ5eEi3qOZ6PnoYVbIPd4f_spE7OkECG9TIA',                // the "anon public" key from Supabase > Project Settings > API (live mode only)
  mockInstrumentUrl: '../instrument/purple-soundbank.html'   // demo mode only; in live mode the instrument comes from private storage
};
