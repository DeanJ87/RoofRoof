import '@testing-library/jest-dom/vitest';
import { beforeAll, afterEach, afterAll } from 'vitest';
import { cleanup } from '@testing-library/preact';
import { server } from './mswServer';
import { forgetCopy } from '../lib/syncView';

beforeAll(() => server.listen({ onUnhandledRequest: 'error' }));
afterEach(() => {
  server.resetHandlers();
  cleanup();
  forgetCopy(); // spec 020: what a copy brought is remembered per page load
});
afterAll(() => server.close());
