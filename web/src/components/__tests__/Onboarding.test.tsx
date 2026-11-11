import { describe, it, expect, vi } from 'vitest';
import { fireEvent, render, screen, within } from '@testing-library/preact';
import { useState } from 'preact/hooks';
import { ChoiceGroup, Mascot, Modal, ProgressMeter, SpeechBubble } from '../ui';

// Spec 018: the design system's onboarding set (DS-11 to DS-13).

const Dialog = ({ onEscape = () => undefined }: { onEscape?: () => void }) => (
  <Modal
    labelledBy="q"
    onEscape={onEscape}
    stopBar={<button type="button">Stop roof</button>}
    header={<button type="button">Back</button>}
    footer={<button type="button">Continue</button>}
    aside={<p>Why</p>}
  >
    <SpeechBubble id="q">Ready?</SpeechBubble>
    <input aria-label="Name" />
  </Modal>
);

describe('Modal', () => {
  it('is a modal dialog named by its heading, with the Stop bar inside', () => {
    render(<Dialog />);
    const dialog = screen.getByRole('dialog', { name: 'Ready?' });
    expect(dialog).toHaveAttribute('aria-modal', 'true');
    expect(within(dialog).getByRole('button', { name: 'Stop roof' })).toBeInTheDocument();
    expect(document.body).toHaveClass('has-modal');
  });

  it('wraps Tab and Shift+Tab inside the dialog, through the Stop bar', () => {
    render(<Dialog />);
    const dialog = screen.getByRole('dialog');
    const stop = screen.getByRole('button', { name: 'Stop roof' });
    const back = screen.getByRole('button', { name: 'Back' });
    stop.focus();
    fireEvent.keyDown(dialog, { key: 'Tab' });
    expect(document.activeElement).toBe(back);
    fireEvent.keyDown(dialog, { key: 'Tab', shiftKey: true });
    expect(document.activeElement).toBe(stop);
  });

  it('Escape calls onEscape and nothing else', () => {
    const onEscape = vi.fn();
    render(<Dialog onEscape={onEscape} />);
    fireEvent.keyDown(screen.getByRole('textbox', { name: 'Name' }), { key: 'Escape' });
    expect(onEscape).toHaveBeenCalledTimes(1);
    expect(screen.getByRole('dialog')).toBeInTheDocument();
  });

  it('gives focus back to where it was, or to the main region', async () => {
    const opener = document.createElement('button');
    document.body.append(opener);
    opener.focus();
    const { unmount } = render(<Dialog />);
    unmount();
    await new Promise((resolve) => setTimeout(resolve, 5));
    expect(document.activeElement).toBe(opener);
    expect(document.body).not.toHaveClass('has-modal');

    const main = document.createElement('main');
    main.id = 'main';
    main.tabIndex = -1;
    document.body.append(main);
    opener.focus();
    const again = render(<Dialog />);
    opener.remove();
    again.unmount();
    await new Promise((resolve) => setTimeout(resolve, 5));
    expect(document.activeElement).toBe(main);
    main.remove();
  });
});

describe('ChoiceGroup', () => {
  const Group = ({ initial = null as string | null }) => {
    const [value, setValue] = useState<string | null>(initial);
    return (
      <>
        <h2 id="q">How many?</h2>
        <ChoiceGroup
          name="count"
          labelledBy="q"
          value={value}
          onChange={setValue}
          options={[
            { value: 'one', title: 'One', detail: 'Just the one.', icon: 'switch', badge: 'Recommended' },
            { value: 'two', title: 'Two', detail: 'Both of them.' },
          ]}
        />
      </>
    );
  };

  it('is a radio group named by the question, with nothing chosen yet', () => {
    render(<Group />);
    const group = screen.getByRole('radiogroup', { name: 'How many?' });
    const radios = within(group).getAllByRole('radio');
    expect(radios).toHaveLength(2);
    expect(radios.every((radio) => !(radio as HTMLInputElement).checked)).toBe(true);
    expect(screen.getByRole('radio', { name: 'One' })).toHaveAccessibleDescription('Recommended Just the one.');
  });

  it('a click chooses, and the chosen card is marked', () => {
    render(<Group />);
    fireEvent.click(screen.getByRole('radio', { name: 'Two' }));
    expect(screen.getByRole('radio', { name: 'Two' })).toBeChecked();
    expect(screen.getByRole('radio', { name: 'Two' }).closest('.choice')).toHaveClass('is-checked');
  });
});

describe('ProgressMeter and Mascot', () => {
  it('the progress bar reads its value as text', () => {
    render(<ProgressMeter value={50} label="Setup progress" valueText="Screen 3 of 6" />);
    const bar = screen.getByRole('progressbar', { name: 'Setup progress' });
    expect(bar).toHaveAttribute('aria-valuenow', '50');
    expect(bar).toHaveAttribute('aria-valuetext', 'Screen 3 of 6');
  });

  it('the mascot is decoration, and small', () => {
    const { container } = render(<Mascot />);
    const svg = container.querySelector('svg')!;
    expect(svg).toHaveAttribute('aria-hidden', 'true');
    expect(svg.outerHTML.length).toBeLessThan(3072);
  });
});
