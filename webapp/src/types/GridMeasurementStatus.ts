export interface GridMeasurementStatus {
    enabled: boolean;
    state: 'never' | 'uploading' | 'success' | 'error';
    skip_reason: 'none' | 'no_network' | 'no_time' | 'no_location' | 'no_data' | 'radio_busy';
    http_status: number;
    error: string;
    last_attempt: number;
    last_success: number;
}