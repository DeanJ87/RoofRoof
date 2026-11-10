import { FunctionalComponent } from 'preact';
import { useState } from 'preact/hooks';
import { useAnnounceChange } from '../lib/a11y';
import { useWebSocket } from '../hooks/useWebSocket';
import { useImageUpload, type UpdateType } from '../hooks/useImageUpload';
import type { SystemStatus } from '../types';
import { Button, Card, Note, ProgressMeter, ReadingRow } from './ui';
import { t } from '../i18n';
import { formatCount } from '../i18n/format';

const firmwareOf = (msg: SystemStatus | null | undefined) => msg?.firmware;

const uploadStatusTone = (status: string) => (/successful/i.test(status) ? 'ok' : /fail|error/i.test(status) ? 'bad' : 'muted');

const Updates: FunctionalComponent = () => {
  const [updateType, setUpdateType] = useState<UpdateType>('firmware');
  const { file, setFile, uploading, uploadProgress, status, setStatus, waitingForReboot, handleUpload } = useImageUpload();

  // Announce how an update ended, once (spec 022 FR-009 edge case).
  useAnnounceChange(status, (text) => (/complete|successful|fail|error/i.test(text) ? text : null));

  const { data: statusMsg } = useWebSocket<SystemStatus>('/ws/status');
  const currentVersion = firmwareOf(statusMsg)?.version;
  const fileName = updateType === 'firmware' ? 'firmware.bin' : 'littlefs.bin';

  return (
    <div class="panel-page compact-page page-enter">
      <Card title={t('updates.manualUpload')} icon="upload" hint={t('updates.flashABinYouBuilt')}>
        <div class="card-body">
          <ReadingRow label={t('updates.installed')} value={currentVersion ? `v${currentVersion}` : '--'} valueClass="tone-cyan" />
          <div class="form-grid">
            <div class="field">
              <label class="field-label" for="upload-type">
                {t('updates.image')}
              </label>
              <select
                id="upload-type"
                class="input"
                value={updateType}
                onChange={(e) => setUpdateType((e.target as HTMLSelectElement).value as UpdateType)}
                disabled={uploading || waitingForReboot}
              >
                <option value="firmware">{t('updates.firmwareFirmwareBin')}</option>
                <option value="filesystem">{t('updates.webUiLittlefsBin')}</option>
              </select>
            </div>
            <div class="field">
              <label class="field-label" for="upload-file">
                {t('updates.file')}
              </label>
              <input
                id="upload-file"
                type="file"
                accept=".bin"
                class="input"
                onChange={(e) => {
                  const files = (e.target as HTMLInputElement).files;
                  setFile(files ? files[0] : null);
                  setStatus('');
                }}
                disabled={uploading || waitingForReboot}
              />
            </div>
          </div>
          {file && <Note>{t('updates.nameFixedKb', { name: file.name, fixed: formatCount(Math.round(file.size / 1024)) })}</Note>}
          {uploading && <ProgressMeter value={uploadProgress} label={t('updates.uploadProgress')} announceSteps />}
          {status && <Note tone={uploadStatusTone(status)}>{status}</Note>}
          <div class="btn-row">
            <Button
              onClick={() => handleUpload(updateType)}
              disabled={!file || waitingForReboot}
              busy={uploading}
              busyLabel={t('common.uploading')}
            >
              {waitingForReboot ? t('updates.waitingForRestart') : t('updates.uploadFilename', { fileName })}
            </Button>
          </div>
        </div>
      </Card>
    </div>
  );
};

export default Updates;
