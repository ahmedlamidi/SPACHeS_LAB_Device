#python script to visualize the heart beat data
# assuming it is called with the correct data
import time
import matplotlib.pyplot as plt
def estimate_spo2(ir_data, ir_length):
    mean_ir_data = sum(ir_data) / ir_length

    # we remove the mean part and also invert the signal
    ac_ir_data = [-1 * (data_point - mean_ir_data) for data_point in ir_data]

    # we are doing a four point moving average now
    for ind in range(len(ac_ir_data) - 3):
        ac_ir_data[ind] = (ac_ir_data[ind] + ac_ir_data[ind + 1] + ac_ir_data[ind + 2] + ac_ir_data[ind + 3]) / 4

    
    smooth = [-1 * data for data in ac_ir_data]
    #now find the average for the ac portion and use that as the minimum peak height

    minimum_peak_height = sum(ac_ir_data) / len(ac_ir_data)


    # we want it to be greater than 30 and less than 60
    minimum_peak_height = min(minimum_peak_height, 60)
    minimum_peak_height = max(minimum_peak_height, 30)


    #call find peaks and store result in valley_indexes
    valley_indexes = find_peak([], 0, ac_ir_data, len(ac_ir_data), minimum_peak_height, 4, 15)

    # calculating actual heartbeat now
    peak_interval_sum = 0
    if  len(valley_indexes)>= 2:
        for index in range(len(valley_indexes) - 1):
            peak_interval_sum += valley_indexes[index + 1] - valley_indexes[index]
    #    for index in range(len(valley_indexes) - 1):
    #         peak_interval_sum += timestamp[valley_indexes[index + 1]] - timestamp[valley_indexes[index]]

        average_interval_sum = (peak_interval_sum / (len(valley_indexes) - 1))
        heart_rate = (25 * 60) / average_interval_sum  # sampling rate * 60 
        # essentailly saying how many average heart beats can we fit if we take 25 points per second 
        # each heartbeat on average takes average interval sum
        # that is assuming uniform sampling frequency
        print(heart_rate)
        print(average_interval_sum)
        return heart_rate, smooth



def find_peak(valley_locations, count_peaks, ac_component_array, array_len, min_peak_height, min_ind_dist_peaks, max_peak_count):
    # call find peak above
    # then call remove close peaks also
    valley_locations = find_peak_above([], 0,ac_component_array, len(ac_component_array), min_peak_height)
    valley_locations = remove_close_peaks(valley_locations, len(valley_locations), ac_component_array, min_ind_dist_peaks)
    return valley_locations


def find_peak_above(peak_locations, count_peaks, data_array,array_len ,min_height_peaks):
    current_ind = 1
    width = 0
    while current_ind < len(data_array):
        if data_array[current_ind] > data_array[current_ind - 1]:
            #removed the min part
            #find left edge of the peak 
            # then also make sure we are greater than the minimum height
            width = 1
            while(current_ind + width < len(data_array) and data_array[current_ind] == data_array[current_ind + width]):
                width += 1
            # we get the flat topped peaks

            if (current_ind + width < len(data_array) and data_array[current_ind] > data_array[current_ind + width]) and count_peaks < 15:
                # check that we are greater than the right side also
                peak_locations.append(current_ind)
                count_peaks += 1
                current_ind += width + 1
            else:
                current_ind += width
        else:
            current_ind += 1
    return peak_locations

        # what do we need to return to make this work in python 



def remove_close_peaks(peak_locations, number_peaks, data_array, min_ind_dist_peaks):
    # so first we make the locations sorted based on the size of the peaks they contain

    # ---------- Old Thinking -----------
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

    #-----------New Thinking ---------
    # we sort the indices by height from large to small
    # so sort ascending is correct right now
    # Then starting from the biggest index we only add it to array
        # on the condition the next one is atleast n - min_distance away
        # or if it is the starting element in the list
        # it incorrectly adds just one - we need to add (min_distance + 1) 


    # ------ Conclusion -----
    # I was wrong in my initial thinking and it basically slowly removes the peaks that are too close
    # still need to change the part of the code with the I

    print(peak_locations , "original")
    sort_indices_descend(data_array, peak_locations)
    index = 0
    while index < len(peak_locations):
        temp_array = peak_locations[:index + 1]  # Keep peaks on the left
        for inner_index in range(index + 1, len(peak_locations)):
            if abs(peak_locations[inner_index] - peak_locations[index]) > min_ind_dist_peaks:
                temp_array.append(peak_locations[inner_index])
        peak_locations = temp_array  # Update the list
        index += 1

    # Restore indices to ascending order
    peak_locations.sort()
    print(peak_locations, "locations_sorted")
    return peak_locations


def sort_indices_descend(data_array, location_peaks):
    location_peaks.sort(key=lambda ind: data_array[ind], reverse=True)
    # sort in descending order based on the data array values


def sort_ascending(array):
    array.sort()
    # just to keep a similar structure


def main():
    Data_Collection = []
    with open("Data.csv", "r") as data_file:
        current_data = []
        count = 0
        for row in data_file:
            row = row.split(";")
            if row[2] == "":
                pass
            elif row[2] != "PPG_R":
                count += 1
                current_data.append(int(row[2]))
                if count == 100:
                    count = 0
                    Data_Collection.append(current_data)
                    current_data = []
        Data_Collection.append(current_data)
        for i in Data_Collection:
            if len(i) != 0:
                estimate_spo2(i, len(i))
        indexes = list(range(len(Data_Collection[1])))
        fig, axes = plt.subplots(2, 1)
        axes[0].plot(indexes, Data_Collection[1])
        heart_rate, smooth  = estimate_spo2(Data_Collection[1], len(Data_Collection[1]))
        axes[1].plot(indexes, smooth)
        
        # plt.plot(indexes,markevery=estimate_spo2(Data_Collection[1], len(Data_Collection[1])), ls="", marker="o", label="points")

        plt.show()

main()
