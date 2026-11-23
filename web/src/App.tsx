import { FunctionalComponent } from 'preact';
import Router from 'preact-router';
import type { CustomHistory } from 'preact-router';
import Layout from './components/Layout';
import Dashboard from './components/Dashboard';
import Settings from './components/Settings';
import System from './components/System';
import Updates from './components/Updates';
import Alpaca from './components/Alpaca';
import NotFound from './components/NotFound';
import WifiSetup from './components/WifiSetup';
import Wizard, { noteRoute } from './components/Wizard';
import Calibrate from './components/Calibrate';
import Diagnostics from './components/Diagnostics';
import Pins from './components/Pins';
import TestBox from './components/TestBox';

const isDemo = import.meta.env.VITE_DEMO_MODE === 'true';

const hashHistory: CustomHistory | undefined = isDemo
  ? {
      get location() {
        return { pathname: window.location.hash.replace(/^#/, '') || '/' } as Location;
      },
      push(path: string) {
        window.location.hash = path;
      },
      replace(path: string) {
        window.location.hash = path;
      },
      listen(cb: (loc: Location) => void) {
        const h = () => cb({ pathname: window.location.hash.replace(/^#/, '') || '/' } as Location);
        window.addEventListener('hashchange', h);
        return () => window.removeEventListener('hashchange', h);
      },
    }
  : undefined;

const App: FunctionalComponent = () => (
  <Router history={hashHistory} onChange={(change) => noteRoute(change.url, change.previous)}>
    <Layout path="/">
      <Dashboard />
    </Layout>
    <Layout path="/alpaca">
      <Alpaca />
    </Layout>
    <Layout path="/settings">
      <Settings />
    </Layout>
    <Layout path="/system">
      <System />
    </Layout>
    <Layout path="/updates">
      <Updates />
    </Layout>
    {/* The setup wizard (spec 018): a dialog over the dashboard. */}
    <Layout path="/wizard" modal={<Wizard />}>
      <Dashboard />
    </Layout>
    <Layout path="/calibrate">
      <Calibrate />
    </Layout>
    {/* Advanced diagnostics (spec 017): from System and Settings, not the main nav. */}
    <Layout path="/diagnostics">
      <Diagnostics />
    </Layout>
    {/* The board and its pins (spec 023): from Settings → Roof and System, not the main nav. */}
    <Layout path="/pins">
      <Pins />
    </Layout>
    {/* Test box mode (spec 022): from its banner, Settings → Roof and the setup wizard. */}
    <Layout path="/test-box">
      <TestBox />
    </Layout>
    <Layout path="/wifi">
      <WifiSetup />
    </Layout>
    <Layout default>
      <NotFound />
    </Layout>
  </Router>
);

export default App;
