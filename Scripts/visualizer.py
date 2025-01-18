#python script to visualize the heart beat data
# assuming it is called with the correct data

def estimate_spo2(ir_data, ir_length, r_data, ts_arr):
    mean_ir_data = sum(ir_data) / ir_length

    # we remove the mean part and also invert the signal
    ac_ir_data = [-1 * (data_point - mean_ir_data) for data_point in ir_data]


    # we are doing a four point moving average now
    for ind in range(len(ac_ir_data) - 3):
        ac_ir_data[ind] = (ac_ir_data[ind] + ac_ir_data[ind + 1] + ac_ir_data[ind + 2] + ac_ir_data[ind + 3])

    
    #now find the average for the ac portion and use that as the minimum peak height

    minimum_peak_height = sum(ac_ir_data) / len(ac_ir_data)


    # we want it to be greater than 30 and less than 60
    minimum_peak_height = min(minimum_peak_height, 60)
    minimum_peak_height = max(minimum_peak_height, 30)

    # create an array to keep the value of the valleys
    valley_indexes = []

    #call find peaks and store result in valley_indexes

    # calculating actual heartbeat now
    peak_interval_sum = 0
    if  len(valley_indexes)>= 2:
        for index in range(len(valley_indexes) - 1):
            peak_interval_sum += valley_indexes[index + 1] - valley_indexes[index]
    #    for index in range(len(valley_indexes) - 1):
    #         peak_interval_sum += timestamp[valley_indexes[index + 1]] - timestamp[valley_indexes[index]]

        average_interval_sum = (peak_interval_sum / len(valley_indexes) - 1)
        heart_rate = (25 * 60) / peak_interval_sum  # sampling rate * 60 
        # essentailly saying how many average heart beats can we fit if we take 25 points per second 
        # each heartbeat on average takes average interval sum
        # that is assuming uniform sampling frequency



def find_peak(valley_locations, count_peaks, ac_component_array, size_array, peak_min_height, peak_min_distance, max_peak_count):
    # call find peak above
    # then call remove close peaks also
    pass


def find_peak_above(peak_locations, count_peaks, data_array,size_array,min_height_peaks):
    current_ind = 1
    width = 0
    while current_ind < len(data_array):
        if data_array[current_ind] > min_height_peaks and data_array[current_ind] > data_array[current_ind - 1]:
            #find left edge of the peak 
            # then also make sure we are greater than the minimum height
            width = 1
            while(current_ind + width < len(data_array) and data_array[current_ind] == data_array[current_ind + width]):
                width += 1
            # we get the flat topped peaks

            if (current_ind + width < len(data_array) or data_array[current_ind] > data_array[current_ind + width]) and count_peaks < 15:
                # check that we are greater than the right side also
                peak_locations.append(current_ind)
                count_peaks += 1
                current_ind += width + 1
            else:
                current_ind += width
        else:
            current_ind += 1

        # what do we need to return to make this work in python 



def remove_close_peaks(location_peaks, number_peaks, data_array, min_distance_peaks):
    # so first we make the locations sorted based on the size of the peaks they contain
    sort_indices_descend(data_array, location_peaks, len(location_peaks))

    # starting i at -1 we go all the way to to the number of peaks we have
    # old number of peaks is then the index + 1
    # essentially starting from the start of the array and putting the peaks back in
    # for all peaks from the next one to the end the array
    # we then check if any of the next peaks is at least n_dist away
    # if it is then we put that peak that is atleast n distance away into the array and increment by one
    # so essentially we are just checking to make sure that the next ones are not too close to us 

    # its dumb because when it finds one far away enough it still checks and loads it in
    # and then does this over the whole array
    # it also keeps them all the first iteration over
    # no idea why it does this though
    
    # optimize here
    new_peaks = []
    past_peak = data_array[location_peaks[0]]
    current_ind = 1
    while current_ind < len(location_peaks):
        if abs(data_array[location_peaks[current_ind]] - past_peak) > min_distance_peaks:
            past_peak = data_array[location_peaks[current_ind]]
            new_peaks.append(location_peaks[current_ind])
        current_ind += 1

    # same functionality but easier to understand I hope
    return new_peaks


def sort_indices_descend(data_array, location_peaks, number_peaks):
    location_peaks.sort(key=lambda ind: data_array[ind], reversed=True)
    # sort in descending order based on the data array values


def sort_ascending(array):
    array.sort()
    # just to keep a similar structure


